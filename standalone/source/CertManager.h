//
// Created by bhare on 4/1/2024.
//

#ifndef CERTMANAGER_H
#define CERTMANAGER_H

#include "czmq.h"
#include <string>

class CertManager {
public:
    inline static const std::string DEFAULT_CERT_FILENAME = "keys.cert";

    ~CertManager();
    bool LoadKeys(const std::string& publicKeyPath);
    bool GenerateKeys(const std::string& certificateFilename);
    bool GenerateKeysFromPrivate(const std::string& privateKey);
    zcert_t* GetCert();

private:
    zcert_t* cert_ = nullptr;
};

#endif // CERTMANAGER_H
