/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2016                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include <string>
#include <numeric>
#include <fstream>
#include <iostream>
#include <iterator>

#define CAF_SUITE streaming
#include "caf/test/dsl.hpp"

#include "caf/io/middleman.hpp"
#include "caf/io/basp_broker.hpp"
#include "caf/io/network/test_multiplexer.hpp"

using std::cout;
using std::endl;
using std::string;

using namespace caf;

namespace {

behavior file_reader(event_based_actor* self) {
  using buf = std::deque<int>;
  return {
    [=](const std::string&) -> stream<int> {
      return self->add_source(
        // initialize state
        [&](buf& xs) {
          xs = buf{1, 2, 3, 4, 5, 6, 7, 8, 9};
        },
        // get next element
        [=](buf& xs, downstream<int>& out, size_t num) {
          auto n = std::min(num, xs.size());
          for (size_t i = 0; i < n; ++i)
            out.push(xs[i]);
          xs.erase(xs.begin(), xs.begin() + static_cast<ptrdiff_t>(n));
        },
        // check whether we reached the end
        [=](const buf& xs) {
          return xs.empty();
        }
      );
    }
  };
}

void streamer(event_based_actor* self, const actor& dest) {
  using buf = std::deque<int>;
  self->new_stream(
    // destination of the stream
    dest,
    // initialize state
    [&](buf& xs) {
      xs = buf{1, 2, 3, 4, 5, 6, 7, 8, 9};
    },
    // get next element
    [=](buf& xs, downstream<int>& out, size_t num) {
      auto n = std::min(num, xs.size());
      for (size_t i = 0; i < n; ++i)
        out.push(xs[i]);
      xs.erase(xs.begin(), xs.begin() + static_cast<ptrdiff_t>(n));
    },
    // check whether we reached the end
    [=](const buf& xs) {
      return xs.empty();
    },
    // handle result of the stream
    [=](expected<int>) {
      // nop
    }
  );
}

behavior filter(event_based_actor* self) {
  return {
    [=](stream<int>& in) -> stream<int> {
      return self->add_stage(
        // input stream
        in,
        // initialize state
        [=](unit_t&) {
          // nop
        },
        // processing step
        [=](unit_t&, downstream<int>& out, int x) {
          if ((x & 0x01) != 0)
            out.push(x);
        },
        // cleanup
        [=](unit_t&) {
          // nop
        }
      );
    }
  };
}

behavior broken_filter(event_based_actor*) {
  return {
    [=](stream<int>& x) -> stream<int> {
      return x;
    }
  };
}

behavior sum_up(event_based_actor* self) {
  return {
    [=](stream<int>& in) {
      return self->add_sink(
        // input stream
        in,
        // initialize state
        [](int& x) {
          x = 0;
        },
        // processing step
        [](int& x, int y) {
          x += y;
        },
        // cleanup and produce result message
        [](int& x) -> int {
          return x;
        }
      );
    }
  };
}

behavior drop_all(event_based_actor* self) {
  return {
    [=](stream<int>& in) {
      return self->add_sink(
        // input stream
        in,
        // initialize state
        [](unit_t&) {
          // nop
        },
        // processing step
        [](unit_t&, int) {
          // nop
        },
        // cleanup and produce void "result"
        [](unit_t&) {
          // nop
        }
      );
    }
  };
}

void streamer_without_result(event_based_actor* self, const actor& dest) {
  using buf = std::deque<int>;
  self->new_stream(
    // destination of the stream
    dest,
    // initialize state
    [&](buf& xs) {
      xs = buf{1, 2, 3, 4, 5, 6, 7, 8, 9};
    },
    // get next element
    [=](buf& xs, downstream<int>& out, size_t num) {
      auto n = std::min(num, xs.size());
      for (size_t i = 0; i < n; ++i)
        out.push(xs[i]);
      xs.erase(xs.begin(), xs.begin() + static_cast<ptrdiff_t>(n));
    },
    // check whether we reached the end
    [=](const buf& xs) {
      return xs.empty();
    },
    // handle result of the stream
    [=](expected<void>) {
      // nop
    }
  );
}

using fixture = test_coordinator_fixture<>;

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(local_streaming_tests, fixture)

CAF_TEST(no_downstream) {
  CAF_MESSAGE("opening streams must fail if no downstream stage exists");
  auto source = sys.spawn(file_reader);
  self->send(source, "test.txt");
  sched.run();
  CAF_CHECK_EQUAL(fetch_result(), sec::no_downstream_stages_defined);
  CAF_CHECK(deref(source).streams().empty());
}

CAF_TEST(broken_pipeline) {
  CAF_MESSAGE("streams must abort if a stage fails to initialize its state");
  auto source = sys.spawn(file_reader);
  auto stage = sys.spawn(broken_filter);
  auto pipeline = stage * source;
  sched.run();
  // self --("test.txt")--> source
  self->send(pipeline, "test.txt");
  expect((std::string), from(self).to(source).with("test.txt"));
  // source --(stream_msg::open)--> stage
  expect((stream_msg::open), from(self).to(stage).with(_, source, _, _, false));
  CAF_CHECK(!deref(source).streams().empty());
  CAF_CHECK(deref(stage).streams().empty());
  // stage --(stream_msg::abort)--> source
  expect((stream_msg::abort),
         from(stage).to(source).with(sec::stream_init_failed));
  CAF_CHECK(deref(source).streams().empty());
  CAF_CHECK(deref(stage).streams().empty());
  CAF_CHECK_EQUAL(fetch_result(), sec::stream_init_failed);
}

CAF_TEST(incomplete_pipeline) {
  CAF_MESSAGE("streams must abort if not reaching a sink");
  auto source = sys.spawn(file_reader);
  auto stage = sys.spawn(filter);
  auto pipeline = stage * source;
  sched.run();
  // self --("test.txt")--> source
  self->send(pipeline, "test.txt");
  expect((std::string), from(self).to(source).with("test.txt"));
  // source --(stream_msg::open)--> stage
  CAF_REQUIRE(sched.prioritize(stage));
  expect((stream_msg::open), from(self).to(stage).with(_, source, _, _, false));
  CAF_CHECK(!deref(source).streams().empty());
  CAF_CHECK(deref(stage).streams().empty());
  // stage --(stream_msg::abort)--> source
  expect((stream_msg::abort),
         from(stage).to(source).with(sec::stream_init_failed));
  CAF_CHECK(deref(source).streams().empty());
  CAF_CHECK(deref(stage).streams().empty());
  CAF_CHECK_EQUAL(fetch_result(), sec::stream_init_failed);
}

CAF_TEST(depth2_pipeline) {
  auto source = sys.spawn(file_reader);
  auto sink = sys.spawn(sum_up);
  auto pipeline = sink * source;
  // run initialization code
  sched.run();
  // self -------("test.txt")-------> source
  self->send(pipeline, "test.txt");
  expect((std::string), from(self).to(source).with("test.txt"));
  CAF_CHECK(!deref(source).streams().empty());
  CAF_CHECK(deref(sink).streams().empty());
  // source ----(stream_msgreturn ::open)----> sink
  expect((stream_msg::open), from(self).to(sink).with(_, source, _, _, false));
  // source <----(stream_msg::ack_open)------ sink
  expect((stream_msg::ack_open), from(sink).to(source).with(5, _, false));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(5, std::vector<int>{1, 2, 3, 4, 5}, 0));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(5, 0));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(4, std::vector<int>{6, 7, 8, 9}, 1));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(4, 1));
  // source ----(stream_msg::close)---> sink
  expect((stream_msg::close), from(source).to(sink).with());
  CAF_CHECK(deref(source).streams().empty());
  CAF_CHECK(deref(sink).streams().empty());
}

CAF_TEST(depth3_pipeline) {
  CAF_MESSAGE("check fully initialized pipeline");
  auto source = sys.spawn(file_reader);
  auto stage = sys.spawn(filter);
  auto sink = sys.spawn(sum_up);
  auto pipeline = self * sink * stage * source;
  // run initialization code
  sched.run();
  // self --("test.txt")--> source
  CAF_CHECK(self->mailbox().empty());
  self->send(pipeline, "test.txt");
  expect((std::string), from(self).to(source).with("test.txt"));
  CAF_CHECK(self->mailbox().empty());
  CAF_CHECK(!deref(source).streams().empty());
  CAF_CHECK(deref(stage).streams().empty());
  CAF_CHECK(deref(sink).streams().empty());
  // source --(stream_msg::open)--> stage
  expect((stream_msg::open), from(self).to(stage).with(_, source, _, _, false));
  CAF_CHECK(!deref(source).streams().empty());
  CAF_CHECK(!deref(stage).streams().empty());
  CAF_CHECK(deref(sink).streams().empty());
  // stage --(stream_msg::open)--> sink
  expect((stream_msg::open), from(self).to(sink).with(_, stage, _, _, false));
  CAF_CHECK(!deref(source).streams().empty());
  CAF_CHECK(!deref(stage).streams().empty());
  CAF_CHECK(!deref(sink).streams().empty());
  // sink --(stream_msg::ack_open)--> stage
  expect((stream_msg::ack_open), from(sink).to(stage).with(5, _, false));
  // stage --(stream_msg::ack_open)--> source
  expect((stream_msg::ack_open), from(stage).to(source).with(5, _, false));
  // source --(stream_msg::batch)--> stage
  expect((stream_msg::batch),
         from(source).to(stage).with(5, std::vector<int>{1, 2, 3, 4, 5}, 0));
  // stage --(stream_msg::batch)--> sink
  expect((stream_msg::batch),
         from(stage).to(sink).with(3, std::vector<int>{1, 3, 5}, 0));
  // stage --(stream_msg::batch)--> source
  expect((stream_msg::ack_batch), from(stage).to(source).with(5, 0));
  // sink --(stream_msg::batch)--> stage
  expect((stream_msg::ack_batch), from(sink).to(stage).with(3, 0));
  // source --(stream_msg::batch)--> stage
  expect((stream_msg::batch),
         from(source).to(stage).with(4, std::vector<int>{6, 7, 8, 9}, 1));
  // stage --(stream_msg::batch)--> sink
  expect((stream_msg::batch),
         from(stage).to(sink).with(2, std::vector<int>{7, 9}, 1));
  // stage --(stream_msg::batch)--> source
  expect((stream_msg::ack_batch), from(stage).to(source).with(4, 1));
  // sink --(stream_msg::batch)--> stage
  expect((stream_msg::ack_batch), from(sink).to(stage).with(2, 1));
  // source ----(stream_msg::close)---> stage
  expect((stream_msg::close), from(source).to(stage).with());
  // stage ----(stream_msg::close)---> sink
  expect((stream_msg::close), from(stage).to(sink).with());
  // sink ----(result: 25)---> self
  expect((int), from(sink).to(self).with(25));
  //auto res = fetch_result<int>();
  //CAF_CHECK_EQUAL(res, 25);
}

CAF_TEST(broken_pipeline_stramer) {
  CAF_MESSAGE("streams must abort if a stage fails to initialize its state");
  auto stage = sys.spawn(broken_filter);
  // run initialization code
  sched.run();
  auto source = sys.spawn(streamer, stage);
  // run initialization code
  sched.run_once();
  // source --(stream_msg::open)--> stage
  expect((stream_msg::open), from(source).to(stage).with(_, source, _, _, false));
  CAF_CHECK(!deref(source).streams().empty());
  CAF_CHECK(deref(stage).streams().empty());
  // stage --(stream_msg::abort)--> source
  expect((stream_msg::abort),
         from(stage).to(source).with(sec::stream_init_failed));
  CAF_CHECK(deref(source).streams().empty());
  CAF_CHECK(deref(stage).streams().empty());
  // sink ----(error)---> source
  expect((error), from(stage).to(source).with(_));
}

CAF_TEST(depth2_pipeline_streamer) {
  auto sink = sys.spawn(sum_up);
  // run initialization code
  sched.run();
  auto source = sys.spawn(streamer, sink);
  // run initialization code
  sched.run_once();
  // source ----(stream_msgreturn ::open)----> sink
  expect((stream_msg::open), from(source).to(sink).with(_, source, _, _, false));
  // source <----(stream_msg::ack_open)------ sink
  expect((stream_msg::ack_open), from(sink).to(source).with(5, _, false));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(5, std::vector<int>{1, 2, 3, 4, 5}, 0));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(5, 0));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(4, std::vector<int>{6, 7, 8, 9}, 1));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(4, 1));
  // source ----(stream_msg::close)---> sink
  expect((stream_msg::close), from(source).to(sink).with());
  // sink ----(result: 25)---> source
  expect((int), from(sink).to(source).with(45));
}

CAF_TEST(stream_without_result) {
  auto sink = sys.spawn(drop_all);
  // run initialization code
  sched.run();
  auto source = sys.spawn(streamer_without_result, sink);
  // run initialization code
  sched.run_once();
  // source ----(stream_msgreturn ::open)----> sink
  expect((stream_msg::open), from(source).to(sink).with(_, source, _, _, false));
  // source <----(stream_msg::ack_open)------ sink
  expect((stream_msg::ack_open), from(sink).to(source).with(5, _, false));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(5, std::vector<int>{1, 2, 3, 4, 5}, 0));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(5, 0));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(4, std::vector<int>{6, 7, 8, 9}, 1));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(4, 1));
  // source ----(stream_msg::close)---> sink
  expect((stream_msg::close), from(source).to(sink).with());
  // sink ----(result: <empty>)---> source
  expect((void), from(sink).to(source).with());
}

CAF_TEST_FIXTURE_SCOPE_END() // local_streaming_tests

namespace {

using namespace caf::io;

class remoting_config : public actor_system_config {
public:
  remoting_config() {
    load<middleman, network::test_multiplexer>();
  }
};

basp_broker* get_basp_broker(middleman& mm) {
  auto hdl = mm.named_broker<basp_broker>(atom("BASP"));
  return dynamic_cast<basp_broker*>(actor_cast<abstract_actor*>(hdl));
}

class sub_fixture : public test_coordinator_fixture<remoting_config> {
public:
  middleman& mm;
  network::test_multiplexer& mpx;
  basp_broker* basp;
  connection_handle conn;
  accept_handle acc;
  sub_fixture* peer = nullptr;
  strong_actor_ptr stream_serv;

  sub_fixture()
      : mm(sys.middleman()),
        mpx(dynamic_cast<network::test_multiplexer&>(mm.backend())),
        basp(get_basp_broker(mm)),
        stream_serv(sys.stream_serv()) {
    // nop
  }

  void publish(actor whom, uint16_t port) {
    auto ma = mm.actor_handle();
    scoped_actor self{sys};
    std::set<std::string> sigs;
    // make sure no pending BASP broker messages are in the queu
    mpx.flush_runnables();
    // trigger middleman actor
    self->send(ma, publish_atom::value, port,
               actor_cast<strong_actor_ptr>(std::move(whom)), std::move(sigs),
               "", false);
    // wait for the message of the middleman actor
    mpx.exec_runnable();
    // fetch response
    self->receive(
      [](uint16_t) {
        // nop
      },
      [&](error& err) {
        CAF_FAIL(sys.render(err));
      }
    );
  }

  actor remote_actor(std::string host, uint16_t port) {
    auto ma = mm.actor_handle();
    scoped_actor self{sys};
    CAF_MESSAGE("remote actor: " << host << ":" << port);
    // make sure no pending BASP broker messages are in the queu
    mpx.flush_runnables();
    // trigger middleman actor
    self->send(ma, connect_atom::value, std::move(host), port);
    // wait for the message of the middleman actor
    mpx.exec_runnable();
    // tell peer to accept the connection
    peer->mpx.accept_connection(peer->acc);
    // run handshake between the two BASP broker instances
    while (mpx.try_exec_runnable() || peer->mpx.try_exec_runnable()
           || mpx.read_data() || peer->mpx.read_data()) {
      // re-run until handhsake is fully completed
    }
    CAF_MESSAGE("fetch remote actor proxy");
    actor result;
    self->receive(
      [&](node_id&, strong_actor_ptr& ptr, std::set<std::string>&) {
        result = actor_cast<actor>(std::move(ptr));
      },
      [&](error& err) {
        CAF_FAIL(sys.render(err));
      }
    );
    return result;
  }
};

} // namespace <anonymous>

CAF_TEST(stream_crossing_the_wire) {
  sub_fixture mars;
  sub_fixture earth;
  mars.peer = &earth;
  earth.peer = &mars;
  // earth (streamer_without_result) streams to mars (drop_all)
  CAF_MESSAGE("spawn drop_all sink on mars");
  auto sink = mars.sys.spawn(drop_all);
  // this test uses two fixtures for keeping state
  earth.conn = connection_handle::from_int(1);
  mars.conn = connection_handle::from_int(2);
  mars.acc = accept_handle::from_int(3);
  // prepare publish and remote_actor calls
  CAF_MESSAGE("prepare connections on earth and mars");
  mars.mpx.prepare_connection(mars.acc, mars.conn, earth.mpx, "mars", 8080,
                              earth.conn);
  // publish sink on mars
  CAF_MESSAGE("publish sink on mars");
  mars.publish(sink, 8080);
  // get a proxy on earth
  CAF_MESSAGE("connect from earth to mars");
  auto proxy = earth.remote_actor("mars", 8080);
  CAF_MESSAGE("got proxy: " << to_string(proxy) << ", spawn streamer on earth");
  earth.sched.run();
  auto source = earth.sys.spawn(streamer_without_result, proxy);
  // run initialization code
  earth.sched.run_once();
  // source ----(stream_msgreturn ::open)----> sink
  expect_on(earth, (stream_msg::open),
            from(source).to(earth.stream_serv).with(_, source, _, _, false));
  /*
  // source <----(stream_msg::ack_open)------ sink
  expect((stream_msg::ack_open), from(sink).to(source).with(5, _, false));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(5, std::vector<int>{1, 2, 3, 4, 5}, 0));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(5, 0));
  // source ----(stream_msg::batch)---> sink
  expect((stream_msg::batch),
         from(source).to(sink).with(4, std::vector<int>{6, 7, 8, 9}, 1));
  // source <--(stream_msg::ack_batch)---- sink
  expect((stream_msg::ack_batch), from(sink).to(source).with(4, 1));
  // source ----(stream_msg::close)---> sink
  expect((stream_msg::close), from(source).to(sink).with());
  // sink ----(result: <empty>)---> source
  expect((void), from(sink).to(source).with());
  */
}
