//
// Created by bhare on 4/2/2024.
//

#ifndef BROKER_H
#define BROKER_H

//#include "mosaic/Broker.h"

#include "malamute.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

using std::string;

#include <fstream>
#include <string>


//  Checks whether client can connect to server
static bool
s_can_connect (zsock_t **server, zsock_t **client, bool renew)
{
    int port_nbr = zsock_bind (*server, "tcp://127.0.0.1:*");
    assert (port_nbr > 0);
    int rc = zsock_connect (*client, "tcp://127.0.0.1:%d", port_nbr);
    assert (rc == 0);
    //  Give the connection time to fail if that's the plan
    if (zsock_mechanism (*client) == ZMQ_CURVE)
        zclock_sleep (1500);
    else
        zclock_sleep (200);

    //  By default PUSH sockets block if there's no peer
    zsock_set_sndtimeo (*client, 200);
    zstr_send (*client, "Hello, World");

    zpoller_t *poller = zpoller_new (*server, NULL);
    assert (poller);
    bool success = (zpoller_wait (poller, 400) == *server);
    zpoller_destroy (&poller);

    if (renew == true)
        throw;

    return success;
}

namespace mosaic {

#define DEFAULT_CONFIG_TEXT R"(
server
    timeout = 60000    # Client connection timeout, msec
    background = 0     # Run as background process
    workdir = .        # Working directory for daemon
    verbose = 0        # Do verbose logging of activity?
auth
    verbose = 1       # Debug authentication steps?
    certstore = ./certstore/

mlm_server
    security
        mechanism = CURVE
    bind
        endpoint = tcp://*:9999
    service
        queue
            size-limit = max    # Maximum size of service queue (in bytes)
            size-warn = max     # Warn if a service queue reaches this size
    mailbox
        size-limit = max    # Maximum mailbox size (integer or "max")
        size-warn = max     # Warn if a mailbox reaches this size
)"

class Broker {
    zactor_t *broker_ = nullptr;
    zactor_t *auth_ = nullptr;
    mlm_client_t *client_ = nullptr;
    string address_;

    string getBrokerEndpoint() {
        //todo switch on region
        return "tcp://127.0.0.1:9999";
    }



    string makePathValid(string path, string defaultPath)
    {
        if (path.empty()) {
            return defaultPath;
        }

        char first_char = path[0];
        if(first_char != '.' && first_char != '/') {
            return "./" + path;
        }

        return path;
    }


    zcertstore_t* createCertStore(string directory)
    {
        zsys_dir_create ("%s", directory.c_str());
        return zcertstore_new (directory.c_str());;
    }

    bool createDefaultConfig(string filename)
    {
        // Check if file exists
        std::filesystem::path file_path(filename);
        if (exists(file_path)) {
            return true;
        }

        // Create directories (if needed)
        create_directories(file_path.parent_path()); // Create parent directories

        // Open the file in write mode
        std::ofstream config_file(filename);
        // Check if file open was successful
        if (!config_file.is_open()) {
            throw std::runtime_error("Unable able to open " + filename + " for writing");
        }

        config_file << DEFAULT_CONFIG_TEXT;
        config_file.close();
        return true;
    }

    bool loadConfigFile(string filename)
    {
        if(!createDefaultConfig(filename)) {
            return false;
        }

        if (0 > zstr_sendx(broker_, "LOAD", filename.c_str(), nullptr)) {
            throw std::runtime_error("Failed to load configuration");
	}

	return true;
    }
public:
    // Constructor initializes data
    Broker(string address, bool verbose) {
        broker_ = zactor_new(mlm_server, nullptr);
        if(verbose) {
            zsock_send (broker_, "s", "VERBOSE");
        }

        loadConfigFile(makePathValid("./etc/broker.cfg", "./config/broker.cfg"));


        address_ = std::move(address);
        client_ = mlm_client_new();
        assert(client_);


        if (!zsys_has_curve ()) {
            throw std::runtime_error("Unable to use CURVE security");
        }

        //todo use zconfig to load  auth/certstore
        auto certStorePath = makePathValid("./etc/certstore/", "./certstore/");

        auto certStore = createCertStore(certStorePath);

        auth_ = zactor_new(zauth, certStore);
        assert (auth_);
        if (verbose) {
            zstr_sendx (auth_, "VERBOSE", NULL);
            zsock_wait (auth_);
        }


        zsock_t *server = zsock_new (ZMQ_DEALER);
        assert (server);
        zsock_t *client = zsock_new (ZMQ_DEALER);
        assert (client);


        zcert_t *server_cert = zcert_new ();
        assert (server_cert);
        zcert_t *client_cert = zcert_new ();
        assert (client_cert);
        const char *server_key = zcert_public_txt (server_cert);


        zcert_save_public (client_cert, (certStorePath + "client.cert").c_str());

        zcert_set_meta (client_cert, "Hello", "%s", "World!");

        zcert_apply (server_cert, server);
        zcert_apply (client_cert, client);

        zsock_set_curve_server (server, 1);
        zsock_set_curve_serverkey (client, server_key);



       // zstr_sendx (auth_, "CURVE", certStorePath.c_str(), NULL);
       // zsock_wait (auth_);


        zsock_set_zap_domain (server, "global");
        int success = s_can_connect (&server, &client, false);
        assert (success);




    }

    ~Broker() {
        mlm_client_destroy(&client_);
        zactor_destroy (&broker_);
        zactor_destroy (&auth_);
    }
};

} // mosaic

#endif //BROKER_H
