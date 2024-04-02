

#include "Mosaic/version.h"
#include "CertManager.h"
#include <iostream>


bool CertManager::LoadKeys(const std::string& publicKeyPath)
{
    cert_ = zcert_load(publicKeyPath.c_str());
    return cert_ != nullptr;
}

bool CertManager::GenerateKeys(const std::string& certificateFilename)
{
    cert_ = zcert_new();
    if (cert_ == nullptr) {
        return false;
    }

    std::cin.clear();

    std::string filename = certificateFilename, email, name, organization;
    if (filename == DEFAULT_CERT_FILENAME) {
        std::cout << "Enter the filename for certificates (default: " + DEFAULT_CERT_FILENAME + "): ";
        std::getline(std::cin, filename);
    }

    if (filename.empty()) {
        filename = DEFAULT_CERT_FILENAME;
    }

    // Prompt user for metadata
    std::cout << "Enter your email address: ";
    std::getline(std::cin, email);
    std::cout << "Enter your name: ";
    std::getline(std::cin, name);
    std::cout << "Enter your organization: ";
    std::getline(std::cin, organization);

    // Set metadata using zcert_set_meta
    zcert_set_meta(cert_, "version", MOSAIC_VERSION);
    zcert_set_meta(cert_, "email", email.c_str());
    zcert_set_meta(cert_, "name", name.c_str());
    zcert_set_meta(cert_, "organization", organization.c_str());

    if (0 < zcert_save(cert_, filename.c_str())) {
        std::cerr << "Failed to save keys." << std::endl;
        zcert_destroy(&cert_);
        return false;
    }
    std::cout << "New public/private key pair generated successfully." << std::endl ;
    std::cout << std::endl ;
    std::cout << "Public key filename: " + filename + "" << std::endl;
    std::cout << "Private key filename:  " + filename + "_secret" << std::endl;
    return true;
}

bool CertManager::GenerateKeysFromPrivate( const std::string& privateKey){
    assert(!privateKey.empty());
    // Implement logic to read private key from standard input and store in cert
    // ... (e.g., using std::cin and CZMQ functions)
    return false; // Placeholder for future implementation
}

zcert_t* CertManager::GetCert(){
    return cert_;
}

CertManager::~CertManager() {
    if (cert_ != nullptr) {
        zcert_destroy(&cert_);
    }
}

