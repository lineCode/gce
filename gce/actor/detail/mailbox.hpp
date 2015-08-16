///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

#ifndef GCE_ACTOR_DETAIL_MAILBOX_HPP
#define GCE_ACTOR_DETAIL_MAILBOX_HPP

#include <gce/actor/config.hpp>
#include <gce/actor/message.hpp>
#include <gce/actor/response.hpp>
#include <gce/actor/pattern.hpp>
#include <gce/actor/detail/mailbox_fwd.hpp>
#include <gce/actor/detail/request.hpp>
#include <gce/detail/scope.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/variant/variant.hpp>
#include <vector>
#include <deque>
#include <list>
#include <map>

namespace gce
{
namespace detail
{
class mailbox
{
  //typedef std::pair<recv_t, message> recv_pair_t;
  typedef recv_pair recv_pair_t;
  //typedef std::pair<resp_t, message*> res_msg_pair_t;
  //typedef std::map<sid_t, res_msg_pair_t> res_msg_list_t;

public:
  mailbox(mailbox_pool_set& pool_set, msg_pool_t& msg_pool, size_t cache_match_size)
    : recv_pair_pool_(pool_set.recv_pair_)
    , recv_itr_pool_(pool_set.recv_itr_)
    , match_queue_pool_(pool_set.match_que_)
    , res_msg_pair_pool_(pool_set.res_msg_pair_)
    , msg_pool_(msg_pool)
    , cache_match_list_(cache_match_size)
  {
    for (size_t i=0, size=cache_match_list_.size(); i<size; ++i)
    {
      cache_match_list_.emplace_back();
      match_queue& que = cache_match_list_.back();
      que.init(make_match(i), recv_itr_pool_);
    }
  }

  ~mailbox()
  {
    recv_que_.clear_and_dispose(recv_pair_disposer(recv_pair_pool_, &msg_pool_));
    res_msg_list_.clear_and_dispose(res_msg_pair_disposer(res_msg_pair_pool_, &msg_pool_));

    for (size_t i=0, size=cache_match_list_.size(); i<size; ++i)
    {
      cache_match_list_[i].on_free();
    }

    match_queue_list_.clear_and_dispose(match_queue_disposer(match_queue_pool_));
  }

public:
  bool pop(recv_t& src, message*& msg, match_list_t const& match_list, recver_t const& recver)
  {
    if (recv_que_.empty())
    {
      return false;
    }

    if (match_list.empty())
    {
      recv_pair_t& rp = recv_que_.front();
      return fetch_match_msg(rp.msg_->get_type(), recver, src, msg);
    }

    BOOST_FOREACH(match_t type, match_list)
    {
      if (fetch_match_msg(type, recver, src, msg))
      {
        return true;
      }
    }

    return false;
  }

  bool pop(resp_t& res, message*& msg)
  {
    dummy3_.sid_= res.get_id();
    res_msg_pair_list_t::iterator itr(res_msg_list_.find(dummy3_));
    if (itr != res_msg_list_.end())
    {
      res = itr->resp_;
      msg = itr->msg_;
      res_msg_list_.erase_and_dispose(itr, res_msg_pair_disposer(res_msg_pair_pool_));
      return true;
    }
    else
    {
      if (aid_t const* aid = res.get_recver<aid_t>())
      {
        aid_t const& recver = *aid;
        exit_list_t::iterator itr(exit_list_.find(recver));
        if (itr != exit_list_.end())
        {
          recv_itr& rtr = *itr->second;
          /*message* tmp = msg_pool_.get();
          *tmp = *rtr.itr_->msg_;
          msg = tmp;*/
          msg = rtr.itr_->msg_;
          erase_exit_msg(itr->second);
          exit_list_.erase(itr);
          res = resp_t(res.get_id(), recver);
          return true;
        }
      }
      else if (svcid_t const* svc = res.get_recver<svcid_t>())
      {
        svcid_t const& recver = *svc;
        svc_exit_list_t::iterator itr(svc_exit_list_.find(recver));
        if (itr != svc_exit_list_.end())
        {
          recv_itr& rtr = *itr->second.second;
          /*message* tmp = msg_pool_.get();
          *tmp = *rtr.itr_->msg_;
          msg = tmp;*/
          msg = rtr.itr_->msg_;
          erase_exit_msg(itr->second.second);
          svc_exit_list_.erase(itr);
          res = resp_t(res.get_id(), itr->second.first);
          return true;
        }
      }
    }

    return false;
  }

  bool pop(aid_t const& aid, request_t& req)
  {
    wait_reply_list_t::iterator itr(wait_reply_list_.find(aid));
    if (itr != wait_reply_list_.end())
    {
      req_queue_t& req_que = itr->second;
      if (!req_que.empty())
      {
        req = req_que.front();
        req_que.pop_front();
        return true;
      }
    }
    return false;
  }

  void push(aid_t const& sender, message* msg)
  {
    recv_t rcv(sender);
    add_match_msg(rcv, sender, msg);
  }

  void push(exit_t const& ex, message* msg)
  {
    recv_t rcv(ex);
    add_match_msg(rcv, ex.get_aid(), msg);
  }

  void push(request_t const& req, message* msg)
  {
    recv_t rcv(req);
    add_match_msg(rcv, aid_nil, msg);
    std::pair<wait_reply_list_t::iterator, bool> pr =
      wait_reply_list_.insert(std::make_pair(req.get_aid(), dummy_));
    pr.first->second.push_back(req);
  }

  bool push(resp_t const& res, message* msg)
  {
    res_msg_pair* rmp = res_msg_pair_pool_.get();
    rmp->init(res.get_id(), res, msg);
    res_msg_list_.insert(*rmp);
    return false;
  }

private:
  void add_match_msg(recv_t const& rcv, aid_t const& sender, message* msg)
  {
    recv_pair_t* rp = recv_pair_pool_.get();
    rp->init(rcv, msg);
    recv_itr* rtr = recv_itr_pool_.get();
    recv_queue_t::iterator itr = recv_que_.insert(recv_que_.end(), *rp);
    rtr->init(itr);
    match_t type = msg->get_type();
    match_itr mtr;
    match_queue_t* match_que = 0;

    if (type.val_ >= 0 && type.val_ < cache_match_list_.size())
    {
      match_que = &cache_match_list_[type.val_].que_;
    }
    else
    {
      match_queue* mq = match_queue_pool_.get();
      mq->init(type, recv_itr_pool_);
      std::pair<match_queue_list_t::iterator, bool> pr =
        match_queue_list_.insert(*mq);
      if (!pr.second)
      {
        match_queue_pool_.free(mq);
      }
      mq = &*pr.first;
      match_que = &mq->que_;
    }
    mtr = match_que->insert(match_que->end(), *rtr);

    if (valid(sender) && boost::get<exit_t>(&rcv) != 0)
    {
      if (sender.svc_ != svcid_nil)
      {
        std::pair<aid_t, match_itr> p = std::make_pair(sender, mtr);
        std::pair<svc_exit_list_t::iterator, bool> pr = 
          svc_exit_list_.insert(std::make_pair(sender.svc_, p));
        //GCE_ASSERT(pr.second);
        if (!pr.second)
        {
          recv_itr& rtr = *pr.first->second.second;
          recv_que_.erase_and_dispose(rtr.itr_, recv_pair_disposer(recv_pair_pool_));
          match_que->erase_and_dispose(pr.first->second.second, recv_itr_disposer(recv_itr_pool_));
          pr.first->second = p;
        }
      }
      else
      {
        std::pair<exit_list_t::iterator, bool> pr = 
          exit_list_.insert(std::make_pair(sender, mtr));
        //GCE_ASSERT(pr.second);
        if (!pr.second)
        {
          recv_itr& rtr = *pr.first->second;
          recv_que_.erase_and_dispose(rtr.itr_, recv_pair_disposer(recv_pair_pool_));
          match_que->erase_and_dispose(pr.first->second, recv_itr_disposer(recv_itr_pool_));
          pr.first->second = mtr;
        }
      }
    }
  }

  bool fetch_match_msg(match_t type, recver_t const& recver, recv_t& src, message*& msg)
  {
    if (fetch_match_msg(type, src, msg))
    {
      return true;
    }
    else
    {
      if (aid_t const* aid = boost::get<aid_t>(&recver))
      {
        exit_list_t::iterator itr(exit_list_.find(*aid));
        if (itr != exit_list_.end())
        {
          recv_itr& rtr = *itr->second;
          src = rtr.itr_->rcv_;
          /*message* tmp = msg_pool_.get();
          *tmp = *rtr.itr_->msg_;
          msg = tmp;*/
          msg = rtr.itr_->msg_;
          erase_exit_msg(itr->second);
          exit_list_.erase(itr);
          return true;
        }
      }
      else if (svcid_t const* svc = boost::get<svcid_t>(&recver))
      {
        svc_exit_list_t::iterator itr(svc_exit_list_.find(*svc));
        if (itr != svc_exit_list_.end())
        {
          recv_itr& rtr = *itr->second.second;
          src = rtr.itr_->rcv_;
          /*message* tmp = msg_pool_.get();
          *tmp = *rtr.itr_->msg_;
          msg = tmp;*/
          msg = rtr.itr_->msg_;
          erase_exit_msg(itr->second.second);
          svc_exit_list_.erase(itr);
          return true;
        }
      }
      return false;
    }
  }

  bool fetch_match_msg(match_t type, recv_t& src, message*& msg)
  {
    std::pair<match_queue_t*, match_queue_list_t::iterator> pr = get_match_queue(type);
    match_queue_t* match_que = pr.first;
    match_queue_list_t::iterator& mq_itr = pr.second;

    if (match_que && !match_que->empty())
    {
      recv_queue_t::iterator itr = match_que->front().itr_;
      match_que->pop_front_and_dispose(recv_itr_disposer(recv_itr_pool_));
      if (match_que->empty() && mq_itr != match_queue_list_.end())
      {
        match_queue_list_.erase_and_dispose(mq_itr, match_queue_disposer(match_queue_pool_));
      }

      src = itr->rcv_;
      msg = itr->msg_;
      if (type == exit)
      {
        aid_t sender;
        if (aid_t* aid = boost::get<aid_t>(&src))
        {
          sender = *aid;
        }
        else if (exit_t* ex = boost::get<exit_t>(&src))
        {
          sender = ex->get_aid();
        }

        if (valid(sender))
        {
          if (sender.svc_ != svcid_nil)
          {
            svc_exit_list_.erase(sender.svc_);
          }
          else
          {
            exit_list_.erase(sender);
          }
        }
      }
      recv_que_.erase_and_dispose(itr, recv_pair_disposer(recv_pair_pool_));
      return true;
    }

    return false;
  }

  std::pair<match_queue_t*, match_queue_list_t::iterator> get_match_queue(match_t type)
  {
    match_queue_t* match_que = 0;
    match_queue_list_t::iterator mq_itr(match_queue_list_.end());
    if (type.val_ >= 0 && type.val_ < cache_match_list_.size())
    {
      match_que = &cache_match_list_[type.val_].que_;
    }
    else
    {
      dummy2_.ty_ = type;
      mq_itr = match_queue_list_.find(dummy2_);
      if (mq_itr != match_queue_list_.end())
      {
        match_que = &mq_itr->que_;
      }
    }
    return std::make_pair(match_que, mq_itr);
  }

  void erase_exit_msg(match_queue_t::iterator& mitr)
  {
    std::pair<match_queue_t*, match_queue_list_t::iterator> pr = get_match_queue(exit);
    match_queue_t* match_que = pr.first;
    match_queue_list_t::iterator& mq_itr = pr.second;
    if (match_que != 0 && !match_que->empty())
    {
      recv_queue_t::iterator itr = mitr->itr_;
      //match_que->pop_front_and_dispose(recv_itr_disposer(recv_itr_pool_));
      match_que->erase_and_dispose(mitr, recv_itr_disposer(recv_itr_pool_));
      if (match_que->empty() && mq_itr != match_queue_list_.end())
      {
        match_queue_list_.erase_and_dispose(mq_itr, match_queue_disposer(match_queue_pool_));
      }
      recv_que_.erase_and_dispose(itr, recv_pair_disposer(recv_pair_pool_));
    }
  }

private:
  recv_pair_pool_t& recv_pair_pool_;
  recv_itr_pool_t& recv_itr_pool_;
  match_queue_pool_t& match_queue_pool_;
  res_msg_pair_pool_t& res_msg_pair_pool_;
  msg_pool_t& msg_pool_;

  //typedef std::list<recv_pair_t> recv_queue_t;
  //typedef recv_queue_t::iterator recv_itr;
  /*struct recv_que_pop_back_binder
  {
    recv_que_pop_back_binder(recv_queue_t& que, recv_pair_pool_t& pool)
      : que_(que)
      , pool_(pool)
    {
    }

    void operator()() const
    {
      que_.pop_back_and_dispose(recv_pair_disposer(pool_));
    }

    recv_queue_t& que_;
    recv_pair_pool_t& pool_;
  };*/
  recv_queue_t recv_que_;

  //typedef std::list<recv_itr> match_queue_t;
  typedef match_queue_t::iterator match_itr;
  dynarray<match_queue> cache_match_list_;

  //typedef std::map<match_t, match_queue_t> match_queue_list_t;
  match_queue_list_t match_queue_list_;

  res_msg_pair_list_t res_msg_list_;

  typedef std::deque<request_t> req_queue_t;
  typedef std::map<aid_t, req_queue_t> wait_reply_list_t;
  wait_reply_list_t wait_reply_list_;

  typedef std::map<aid_t, match_itr> exit_list_t;
  typedef std::map<svcid_t, std::pair<aid_t, match_itr> > svc_exit_list_t;
  exit_list_t exit_list_;
  svc_exit_list_t svc_exit_list_;

  req_queue_t dummy_;
  match_queue dummy2_;
  res_msg_pair dummy3_;
};
}
}

#endif /// GCE_ACTOR_DETAIL_MAILBOX_HPP
