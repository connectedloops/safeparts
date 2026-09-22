#include "bridge.rs.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
rust::Slice<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

std::string stringFrom(const rust::Vec<std::uint8_t> &value) {
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

std::string fixture(const char *relativePath) {
    std::ifstream input(std::string(SAFEPARTS_REPO_ROOT) + "/" + relativePath);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void wipe(rust::Vec<std::uint8_t> &value) {
    std::fill(value.begin(), value.end(), std::uint8_t{0});
}
} // namespace

int main() {
    constexpr char kExact[] = "\0 leading\nline\xC2\xA0space\xE2\x80\xA8separator\xE2\x80\xA9paragraph\n";
    const std::string exact(kExact, sizeof(kExact) - 1);
    auto operation = new_operation();

    const auto created = operation->create(1, bytes(exact), 2, 3, ShareEncoding::MnemoWords);
    if (created.status != Status::Ok || created.share_count != 3) {
        std::cerr << "create contract failed\n";
        return 1;
    }
    const auto first = operation->encode_share(1, 0);
    const auto second = operation->encode_share(1, 1);
    if (first.status != Status::Ok || second.status != Status::Ok) {
        std::cerr << "on-demand encode contract failed\n";
        return 2;
    }

    operation->reset(2);
    const auto malformed = operation->add_recovery(2, bytes("not a recovery share"),
                                                    ShareEncoding::MnemoWords);
    if (malformed.status != Status::MalformedInput || malformed.recovery_batch_count != 1) {
        std::cerr << "handled error contract failed\n";
        return 3;
    }
    operation->remove_recovery_batch(2, 0);
    const std::string trailing = stringFrom(first.bytes) + " abandon";
    const auto trailingResult = operation->add_recovery(2, bytes(trailing),
                                                         ShareEncoding::MnemoWords);
    if (trailingResult.status != Status::MalformedInput || trailingResult.ready) {
        std::cerr << "trailing content contract failed\n";
        return 4;
    }
    operation->remove_recovery_batch(2, 0);
    const auto unsupportedVersion = operation->add_recovery(
        2, bytes("U01OMWMAAgMBrHCYZ3AswOEZZ2pD18UoyAAAAE4PXdVG9ykm_tyf_J5-HJ-_0WtdkvNhn-0vZQL6QwI6H7UZR6ES2tASrurEc-tXOUXN_QPIZSMQAQ1BHoHSBc8k5MjGtaOkiHOGApBppcI"), ShareEncoding::Base64url);
    if (unsupportedVersion.status != Status::UnsupportedVersion || unsupportedVersion.ready) {
        std::cerr << "unsupported version contract failed status="
                  << static_cast<int>(unsupportedVersion.status) << "\n";
        return 5;
    }
    operation->remove_recovery_batch(2, 0);
    const auto unsupportedKdf = operation->add_recovery(
        2,
        bytes("U01OMQIBAgMBpiSUH6ekz9GcJdCrKpkGH24eeWogJbvDcCcKN9uxjNWCI05_7Q4hM1NJ4sT_____AAAAAwAAAAEAAABcQjI9PNRa1FImCFhZTcIkA0oXbyY0awhThV4creXuLHGrQzLFlBAFceaWWlXf-sBzo3dqtrw2yJMzauf0sIGStCqQ3gqrLNj2xZV0kuz2E4BpTYiU6TtYpDghmXo"),
        ShareEncoding::Base64url);
    if (unsupportedKdf.status != Status::UnsupportedParameters || unsupportedKdf.ready) {
        std::cerr << "unsupported KDF contract failed\n";
        return 6;
    }
    operation->remove_recovery_batch(2, 0);
    operation->add_recovery(2, {first.bytes.data(), first.bytes.size()},
                            ShareEncoding::MnemoWords);
    const auto duplicate = operation->add_recovery(
        2, {first.bytes.data(), first.bytes.size()}, ShareEncoding::MnemoWords);
    if (duplicate.status != Status::DuplicateShare || duplicate.ready) {
        std::cerr << "duplicate contract failed\n";
        return 5;
    }
    operation->remove_recovery_batch(2, 1);
    const auto ready = operation->add_recovery(
        2, {second.bytes.data(), second.bytes.size()}, ShareEncoding::MnemoWords);
    if (ready.status != Status::Ok || !ready.ready) {
        std::cerr << "readiness contract failed\n";
        return 6;
    }
    const auto recovered = operation->recover(2);
    if (recovered.status != Status::Ok || stringFrom(recovered.bytes) != exact) {
        std::cerr << "exact recovery contract failed\n";
        return 7;
    }

    auto other = new_operation();
    const std::string otherText = "synthetic other CXX set";
    if (other->create(3, bytes(otherText), 2, 3, ShareEncoding::MnemoWords).status
        != Status::Ok) {
        std::cerr << "other set create failed\n";
        return 8;
    }
    const auto otherFirst = other->encode_share(3, 0);
    operation->reset(4);
    operation->add_recovery(4, {first.bytes.data(), first.bytes.size()},
                            ShareEncoding::MnemoWords);
    const auto mixed = operation->add_recovery(
        4, {otherFirst.bytes.data(), otherFirst.bytes.size()}, ShareEncoding::MnemoWords);
    if (mixed.status != Status::MixedShareSet || mixed.ready) {
        std::cerr << "mixed set contract failed\n";
        return 9;
    }

    const std::array<ShareEncoding, 4> encodings = {
        ShareEncoding::Base64url,
        ShareEncoding::Base58check,
        ShareEncoding::MnemoWords,
        ShareEncoding::MnemoBip39,
    };
    for (ShareEncoding encoding : encodings) {
        auto encodedOperation = new_operation();
        const auto formatCreated = encodedOperation->create(5, bytes(exact), 2, 3, encoding);
        const auto formatFirst = encodedOperation->encode_share(5, 0);
        const auto formatSecond = encodedOperation->encode_share(5, 1);
        encodedOperation->reset(6);
        encodedOperation->add_recovery(
            6, {formatFirst.bytes.data(), formatFirst.bytes.size()}, ShareEncoding::Auto);
        const auto formatReady = encodedOperation->add_recovery(
            6, {formatSecond.bytes.data(), formatSecond.bytes.size()}, ShareEncoding::Auto);
        const auto formatRecovered = encodedOperation->recover(6);
        if (formatCreated.status != Status::Ok || formatReady.status != Status::Ok
            || formatReady.encoding != encoding || formatRecovered.status != Status::Ok
            || stringFrom(formatRecovered.bytes) != exact) {
            std::cerr << "encoding-aware CXX contract failed\n";
            return 10;
        }
        destroy_operation(std::move(encodedOperation));
    }

    struct FixtureCase {
        const char *path;
        ShareEncoding encoding;
    };
    const std::array<FixtureCase, 8> releasedFixtures = {{
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/base64url.txt", ShareEncoding::Base64url},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/base58check.txt", ShareEncoding::Base58check},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/mnemo-words.txt", ShareEncoding::MnemoWords},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/mnemo-bip39.txt", ShareEncoding::MnemoBip39},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/base64url.txt", ShareEncoding::Base64url},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/base58check.txt", ShareEncoding::Base58check},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/mnemo-words.txt", ShareEncoding::MnemoWords},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/mnemo-bip39.txt", ShareEncoding::MnemoBip39},
    }};
    for (const FixtureCase &fixtureCase : releasedFixtures) {
        const std::string text = fixture(fixtureCase.path);
        for (ShareEncoding requested : {ShareEncoding::Auto, fixtureCase.encoding}) {
            auto fixtureOperation = new_operation();
            const auto inspected = fixtureOperation->add_recovery(7, bytes(text), requested);
            if (text.empty() || inspected.status != Status::Ok || !inspected.ready
                || inspected.encoding != fixtureCase.encoding
                || fixtureOperation->recover(7).status != Status::InvalidUtf8) {
                std::cerr << "released fixture CXX contract failed\n";
                return 11;
            }
            destroy_operation(std::move(fixtureOperation));
        }
    }

    struct SurfaceFixtureCase {
        const char *path;
        const char *expected;
    };
    const std::array<SurfaceFixtureCase, 12> surfaceFixtures = {{
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/cli/base64url.txt", "synthetic CLI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/cli/base58check.txt", "synthetic CLI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/cli/mnemo-words.txt", "synthetic CLI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/cli/mnemo-bip39.txt", "synthetic CLI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/tui/base64url.txt", "synthetic TUI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/tui/base58check.txt", "synthetic TUI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/tui/mnemo-words.txt", "synthetic TUI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/tui/mnemo-bip39.txt", "synthetic TUI interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/web/base64url.txt", "synthetic web interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/web/base58check.txt", "synthetic web interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/web/mnemo-words.txt", "synthetic web interoperability"},
        {"crates/safeparts_core/tests/fixtures/surface_interoperability/web/mnemo-bip39.txt", "synthetic web interoperability"},
    }};
    for (const SurfaceFixtureCase &fixtureCase : surfaceFixtures) {
        const std::string text = fixture(fixtureCase.path);
        auto fixtureOperation = new_operation();
        const auto inspected =
            fixtureOperation->add_recovery(8, bytes(text), ShareEncoding::Auto);
        const auto recovered = fixtureOperation->recover(8);
        if (text.empty() || inspected.status != Status::Ok || !inspected.ready
            || recovered.status != Status::Ok
            || stringFrom(recovered.bytes) != fixtureCase.expected) {
            std::cerr << "surface fixture CXX contract failed\n";
            return 12;
        }
        destroy_operation(std::move(fixtureOperation));
    }

    const std::array<FixtureCase, 4> protectedReleasedFixtures = {{
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/base64url.txt", ShareEncoding::Base64url},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/base58check.txt", ShareEncoding::Base58check},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/mnemo-words.txt", ShareEncoding::MnemoWords},
        {"crates/safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/mnemo-bip39.txt", ShareEncoding::MnemoBip39},
    }};
    const std::string releasedPassphrase = "issue-60 synthetic fixture passphrase";
    const std::string releasedSecret("\xffSafeparts V2 protected synthetic Secret\0\x01\x02\n", 44);
    for (const FixtureCase &fixtureCase : protectedReleasedFixtures) {
        const std::string text = fixture(fixtureCase.path);
        for (ShareEncoding requested : {ShareEncoding::Auto, fixtureCase.encoding}) {
            auto protectedOperation = new_operation();
            const auto inspected = protectedOperation->add_recovery(9, bytes(text), requested);
            const auto recovered = protectedOperation->recover_bytes_with_passphrase(9, bytes(releasedPassphrase));
            if (inspected.status != Status::PassphraseRequired || !inspected.passphrase_protected
                || inspected.encoding != fixtureCase.encoding || recovered.status != Status::Ok
                || stringFrom(recovered.bytes) != releasedSecret) {
                std::cerr << "protected released fixture CXX contract failed\n";
                return 13;
            }
            const auto text = protectedOperation->recovered_text(10);
            if (text.status != Status::InvalidUtf8 || !text.bytes.empty()) {
                std::cerr << "binary recovery leaked through text accessor\n";
                return 14;
            }
            destroy_operation(std::move(protectedOperation));
        }
    }

    const std::string binarySecret("\0\xff\xfe\r\nCXX-binary", 15);
    const std::string binaryPassphrase = "synthetic CXX binary passphrase";
    for (ShareEncoding encoding : encodings) {
        auto binaryOperation = new_operation();
        const auto binaryCreated = binaryOperation->create_with_passphrase(
            11, bytes(binarySecret), 2, 3, encoding, bytes(binaryPassphrase));
        auto binaryFirst = binaryOperation->encode_share(11, 0);
        auto binaryFirstAgain = binaryOperation->encode_share(11, 0);
        auto binarySecond = binaryOperation->encode_share(11, 1);
        if (binaryCreated.status != Status::Ok || binaryFirst.status != Status::Ok
            || stringFrom(binaryFirst.bytes) != stringFrom(binaryFirstAgain.bytes)) {
            std::cerr << "stable protected binary create failed\n";
            return 15;
        }
        binaryOperation->reset(12);
        binaryOperation->add_recovery(
            12, {binaryFirst.bytes.data(), binaryFirst.bytes.size()}, ShareEncoding::Auto);
        const auto binaryReady = binaryOperation->add_recovery(
            12, {binarySecond.bytes.data(), binarySecond.bytes.size()}, ShareEncoding::Auto);
        auto binaryRecovered = binaryOperation->recover_bytes_with_passphrase(
            12, bytes(binaryPassphrase));
        auto binaryAccessor = binaryOperation->recovered_bytes(12);
        auto binaryText = binaryOperation->recovered_text(12);
        if (binaryReady.status != Status::PassphraseRequired
            || binaryRecovered.status != Status::Ok
            || stringFrom(binaryRecovered.bytes) != binarySecret
            || binaryAccessor.status != Status::Ok
            || stringFrom(binaryAccessor.bytes) != binarySecret
            || binaryText.status != Status::InvalidUtf8 || !binaryText.bytes.empty()) {
            std::cerr << "exact protected binary recovery failed\n";
            return 16;
        }
        wipe(binaryFirst.bytes);
        wipe(binaryFirstAgain.bytes);
        wipe(binarySecond.bytes);
        wipe(binaryRecovered.bytes);
        wipe(binaryAccessor.bytes);
        destroy_operation(std::move(binaryOperation));
    }

    // Generated once through public split_secret(b"", 1, 1, None) and Base64url encoding.
    const std::string emptyCompatible =
        "U01OMQIAAQEBFH3mxqWB2Xj8LRQg38V3fwAAACCvE0m59fmhpqBATeo23MlJm8slya3BErfMmpPK5B8yYg";
    auto emptyOperation = new_operation();
    const auto emptyReady = emptyOperation->add_recovery(
        13, bytes(emptyCompatible), ShareEncoding::Base64url);
    auto emptyRecovered = emptyOperation->recover_bytes_with_passphrase(13, bytes(""));
    auto emptyAccessor = emptyOperation->recovered_bytes(13);
    auto emptyText = emptyOperation->recovered_text(13);
    if (emptyReady.status != Status::Ok || emptyRecovered.status != Status::Ok
        || !emptyRecovered.bytes.empty() || emptyAccessor.status != Status::Ok
        || !emptyAccessor.bytes.empty() || emptyText.status != Status::Ok
        || !emptyText.bytes.empty()) {
        std::cerr << "valid empty recovery presence failed\n";
        return 17;
    }
    destroy_operation(std::move(emptyOperation));

    const std::array<std::string, 2> exactByteForms = {
        std::string("\xef\xbb\xbf" "BOM\r\nNUL\0tail\n", 17),
        std::string("NFC \xc3\xa9 / NFD e\xcc\x81\r\n"),
    };
    for (const std::string &form : exactByteForms) {
        auto formOperation = new_operation();
        if (formOperation->create(14, bytes(form), 1, 1, ShareEncoding::Base64url).status
            != Status::Ok) {
            std::cerr << "exact byte-form create failed\n";
            return 18;
        }
        auto formShare = formOperation->encode_share(14, 0);
        formOperation->reset(15);
        formOperation->add_recovery(
            15, {formShare.bytes.data(), formShare.bytes.size()}, ShareEncoding::Auto);
        auto formRecovered = formOperation->recover_bytes_with_passphrase(15, bytes(""));
        if (formRecovered.status != Status::Ok || stringFrom(formRecovered.bytes) != form) {
            std::cerr << "BOM/CRLF/NUL/normalization fidelity failed\n";
            return 19;
        }
        wipe(formShare.bytes);
        wipe(formRecovered.bytes);
        destroy_operation(std::move(formOperation));
    }

    destroy_operation(std::move(other));
    destroy_operation(std::move(operation));
    std::cout << "CXX_DESKTOP_BOUNDARY_OK shares=3 threshold=2 encodings=4 released_fixtures=12 surface_fixtures=12 binary=yes empty=yes byte_forms=yes auto=yes handled_errors=yes explicit_release=yes\n";
    return 0;
}
