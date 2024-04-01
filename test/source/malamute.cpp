#include "malamute.h"

#include <doctest/doctest.h>

/*  =========================================================================
    malamute - All the enterprise messaging patterns in one box

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of the Malamute Project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

// we use char* text in message extract text from zmsg
static zpoller_t* poller = nullptr;

// receive message
static zmsg_t* my_mlm_client_recv(mlm_client_t* client)
{
    if (zsys_interrupted)
	return nullptr;

    if (!poller) {
	poller = zpoller_new(mlm_client_msgpipe(client), NULL);
    }

    auto* which = static_cast<zsock_t*>(zpoller_wait(poller, 1000));
    if (which == mlm_client_msgpipe(client)) {
	zmsg_t* reply = mlm_client_recv(client);
	return reply;
    }

    return nullptr;
}

zactor_t* create_broker()
{
    //  Let's start a new Malamute broker
    zactor_t* broker = zactor_new(mlm_server, nullptr);

    //  Switch on verbose tracing... this gets a little overwhelming so you
    //  can comment or delete this when you're bored with it:
    //  zsock_send(broker, "s", "VERBOSE");

    //  We control the broker by sending it commands. It's a CZMQ actor, and
    //  we can talk to it using the zsock API (or zstr, or zframe, or zmsg).
    //  To get things started, let's tell the broker to bind to an endpoint:
    zsock_send(broker, "ss", "BIND", MLM_DEFAULT_ENDPOINT);

    //  We can also, or alternatively, set server properties by sending it
    //  SET commands like this (see malamute.cfg for details):
    zsock_send(broker, "sss", "SET", "server/timeout", "5000");

    return broker;
}

TEST_CASE("Malamute Send/Recv")
{
    zactor_t* broker = create_broker();

    mlm_client_t* client = mlm_client_new();
    CHECK(0 == mlm_client_connect(client, MLM_DEFAULT_ENDPOINT, 1000, "client"));

    mlm_client_t* server = mlm_client_new();
    CHECK(0 == mlm_client_connect(server, MLM_DEFAULT_ENDPOINT, 1000, "server"));

    int reply_nbr = 1;
    for (int request_nbr = 1; request_nbr <= 3; ++request_nbr) {
	zmsg_t* msg = zmsg_new();
	zmsg_addstrf(msg, "msg%d", request_nbr);
	zsys_info("Sending request number=%d", request_nbr);
	mlm_client_sendto(client, "server", "something", nullptr, 1000, &msg);

	zmsg_t* sent_msg = mlm_client_recv(server);
	if (sent_msg) {
	    char* request = zmsg_popstr(sent_msg);
	    zsys_info("server received: %s sender=%s", request, mlm_client_sender(server));
	    zmsg_addstrf(sent_msg, "%s OK (%d)", request, reply_nbr++);
	    mlm_client_sendto(server, mlm_client_sender(server), "anything", nullptr, 0, &sent_msg);
	}

	msg = my_mlm_client_recv(client);
	if (msg) {
	    char* reply = zmsg_popstr(msg);
	    zsys_info("client received: %s", reply);
	    free(reply);
	    zmsg_destroy(&msg);
	    zclock_sleep(100);
	} else
	    zsys_error("MISSED reply number=%d", request_nbr);
    }

    mlm_client_destroy(&client);
    mlm_client_destroy(&server);
    zpoller_destroy(&poller);
    zactor_destroy(&broker);
}