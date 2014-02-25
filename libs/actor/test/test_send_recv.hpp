﻿///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

namespace gce
{
class send_recv_ut
{
public:
  static void run()
  {
    std::cout << "send_recv_ut begin." << std::endl;
    test_base();
    std::cout << "send_recv_ut end." << std::endl;
  }

public:
  static void ping_pong(self_t self)
  {
    try
    {
      aid_t host, partner;
      host = recv(self, atom("prepare"), partner);

      int count_down;
      while (true)
      {
        recv(self, atom("ping_pong"), count_down);
        if (count_down == 0)
        {
          break;
        }
        --count_down;
        send(self, partner, atom("ping_pong"), count_down);
      }
      send(self, host);
    }
    catch (std::exception& ex)
    {
      std::cerr << ex.what() << std::endl;
    }
  }

  static void test_base()
  {
    try
    {
      context ctx;

      int const count_down = 10000;
      mixin_t host = spawn(ctx);
      aid_t ping = spawn(host, boost::bind(&send_recv_ut::ping_pong, _1));
      aid_t pong = spawn(host, boost::bind(&send_recv_ut::ping_pong, _1));

      send(host, ping, atom("prepare"), pong);
      send(host, pong, atom("prepare"), ping);

      send(host, ping, atom("ping_pong"), count_down);
      recv(host);
    }
    catch (std::exception& ex)
    {
      std::cerr << ex.what() << std::endl;
    }
  }
};
}
