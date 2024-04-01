#include <mosaic/mosaic.h>
#include <mosaic/version.h>

#include <cxxopts.hpp>
#include <iostream>

#include "KeyManager.h"
#include <fstream>

bool Parse(
    const cxxopts::ParseResult& result,
    const std::unordered_map<std::string, mosaic::LanguageCode>& languages,
    const std::string& lang_code,
    const std::string& certificate_filename,
    KeyManager* key_manager,
    const std::string& help)
{
    if (result["help"].as<bool>()) {
	std::cout << help << std::endl;
	return true;
    }

    if (result["version"].as<bool>()) {
	std::cout << "Mosaic, version " << MOSAIC_VERSION << std::endl;
	return true;
    }

    auto langIt = languages.find(lang_code);
    if (langIt == languages.end()) {
	std::cerr << "unknown language code: " << lang_code << std::endl;
	return false;
    }

    if (result["generateKeys"].as<bool>()) {
	if (key_manager->GenerateKeys(certificate_filename)) {
	    std::cerr << "Failed to generate key pair." << std::endl;
	    return false;
	}
    } else if (result.count("privateKey")) {
	std::cerr << "Pasting private key from standard input is not implemented yet." << std::endl;
	return false;
    }

    return true;
}

auto main(int argc, char** argv) -> int
{
    const std::unordered_map<std::string, mosaic::LanguageCode> languages {
	{ "en", mosaic::LanguageCode::EN },
	{ "de", mosaic::LanguageCode::DE },
	{ "es", mosaic::LanguageCode::ES },
	{ "fr", mosaic::LanguageCode::FR },
    };

    std::string lang_code;
    std::string certificate_filename;
    std::string private_key;
    zcert_t* cert = nullptr;

    cxxopts::Options options(*argv, "A program to welcome the world!");

    // clang-format off
    options.add_options()
    ("h,help", "Show help")
    ("v,verbose", "Run in verbose mode")
    ("V,version", "Print the current version number")
    ("l,lang", "Language code to use", cxxopts::value(lang_code)->default_value("en"));

    options.add_options("certificate")
    ("c,cert_file", "Public key file (with private key as _secret)", cxxopts::value(certificate_filename)->default_value(KeyManager::DEFAULT_CERT_FILENAME))
    ("g,generateKeys", "Generate new public/private key pair")
    ("p,privateKey", "Private key string", cxxopts::value(private_key));
    // clang-format on

    auto quickHelp = "try 'mosiac --help' for more information"; // todo: Make this .exe or OS dependent
    auto key_manager = new KeyManager();

    try {
	auto result = options.parse(argc, argv);
	auto help = options.help({ "", "certificate" });

	if (!Parse(result, languages, lang_code, certificate_filename, key_manager, help))
	    return 1;

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

    // Try loading keys from files
    if (!key_manager->LoadKeys(certificate_filename)) {
	// No options provided or keys not found, ask user
	int choice;
	std::cout << "Attempted to open " << certificate_filename << ", but keys not found. Would you like to:" << std::endl;
	std::cout << "1. Generate new public and private keypair" << std::endl;
	std::cout << "2. Paste private key and generate keypair" << std::endl;
	std::cout << "3. Exit" << std::endl;
	std::cin >> choice;

	if (choice == 1) {
	    if (!key_manager->GenerateKeys(certificate_filename)) {
		std::cerr << "Failed to generate key pair." << std::endl;
		return 1;
	    }
	} else if (choice == 2) {
	    if (!key_manager->GenerateKeysFromPrivate(private_key)) {
		std::cerr << "Failed to generate key pair." << std::endl;
		return 1;
	    }
	} else if (choice == 3) {
	    return 0;
	} else {
	    std::cerr << "Invalid choice." << std::endl;
	    return 1;
	}
    }

    // You can use the loaded certificate (cert) here for further CZMQ operations
    // ...

    std::cout << zcert_public_txt(key_manager->GetCert()) << std::endl;

    zcert_destroy(&cert);

    return 0;
}
