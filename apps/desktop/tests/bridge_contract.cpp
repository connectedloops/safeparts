#include "bridge.rs.h"

#include <array>
#include <cstdint>
#include <iostream>
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

    destroy_operation(std::move(other));
    destroy_operation(std::move(operation));
    std::cout << "CXX_DESKTOP_BOUNDARY_OK shares=3 threshold=2 encodings=4 auto=yes handled_errors=yes explicit_release=yes\n";
    return 0;
}
