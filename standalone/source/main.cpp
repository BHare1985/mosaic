#include "mosaic/version.h"
#include "CertManager.h"
#include <cxxopts.hpp>
#include <fstream>
#include <iostream>

int prompt_cert_choices(CertManager* cert_manager, const std::string& certificate_filename, const std::string& private_key)
{
    if (cert_manager->LoadKeys(certificate_filename)) {
	return true;
    }

    int choice;
    std::cout << "Attempted to open " << certificate_filename << ", but keys not found. Would you like to:" << std::endl;
    std::cout << "1) Generate new public and private keypair" << std::endl;
    std::cout << "2) Paste private key and generate keypair" << std::endl;
    std::cout << "3) Exit" << std::endl;
    std::cin >> choice;

    switch (choice) {
    case 1:
	return cert_manager->GenerateKeys(certificate_filename);
    case 2:
	return cert_manager->GenerateKeysFromPrivate(private_key);
    case 3:
	exit(0);
    default:
	std::cerr << "Invalid choice." << std::endl;
	return false;
    }
}

auto main(int argc, char** argv) -> int
{
    std::string certificate_filename;
    std::string private_key;

    cxxopts::Options options(*argv, "Mosaic: Decentralized data parity broker");

    // clang-format off
    options.add_options()
    ("h,help", "Show help")
    ("v,verbose", "Run in verbose mode")
    ("V,version", "Print the current version number");

    options.add_options("certificate")
    ("c,cert_file", "Public key file (with private key as _secret)", cxxopts::value(certificate_filename)->default_value(CertManager::DEFAULT_CERT_FILENAME))
    ("g,generateKeys", "Generate new public/private key pair")
    ("p,privateKey", "Private key string", cxxopts::value(private_key));
    // clang-format on

    auto quickHelp = "try 'mosiac --help' for more information"; // todo: Make this .exe or OS dependent
    auto cert_manager = new CertManager();

    try {
	auto result = options.parse(argc, argv);
	auto help = options.help({ "", "certificate" });

	if (result["help"].as<bool>()) {
	    std::cout << help << std::endl;
	    return true;
	}

	if (result["version"].as<bool>()) {
	    std::cout << "Mosaic, version " << MOSAIC_VERSION << std::endl;
	    return true;
	}

	if (result["generateKeys"].as<bool>()) {
	    if (!cert_manager->GenerateKeys(certificate_filename)) {
		std::cerr << "Failed to generate key pair." << std::endl;
		return false;
	    }
	} else if (result.count("privateKey")) {
	    std::cerr << "Pasting private key from standard input is not implemented yet." << std::endl;
	    return false;
	}

	// Handle unknown commands
	if (!result.unmatched().empty()) {
	    std::cerr << "Error: Unknown command(s): ";
	    for (const auto& arg : result.unmatched()) {
		std::cerr << arg << " ";
	    }
	    std::cerr << std::endl;
	    std::cerr << quickHelp << std::endl;
	    return 1;
	}
    } catch (const cxxopts::OptionParseException& e) {
	std::cerr << "Error parsing options: " << e.what() << std::endl;
	std::cerr << quickHelp << std::endl;
	return 1;
    }

    if (!prompt_cert_choices(cert_manager, certificate_filename, private_key)) {
	return 1;
    }

    std::cout << zcert_public_txt(cert_manager->GetCert()) << std::endl;

    return 0;
}
