//
// Created by bhare on 4/1/2024.
//

#ifndef KEYMANAGER_H
#define KEYMANAGER_H

#include<string>
#include "czmq.h"

class KeyManager {
private:
    zcert_t* cert_ = nullptr;

public:
    inline static const std::string DEFAULT_CERT_FILENAME = "keys.cert";

    ~KeyManager();
    bool LoadKeys(const std::string& publicKeyPath);
    bool GenerateKeys(const std::string& certificateFilename);
    bool GenerateKeysFromPrivate(const std::string& privateKey);
    zcert_t* GetCert();

};


#endif //KEYMANAGER_H
