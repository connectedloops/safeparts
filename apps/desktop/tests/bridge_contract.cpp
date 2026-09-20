#include "bridge.rs.h"

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

    const auto created = operation->create_words(1, bytes(exact), 2, 3);
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
    const auto malformed = operation->add_recovery_words(2, bytes("not a recovery share"));
    if (malformed.status != Status::MalformedInput || malformed.recovery_batch_count != 1) {
        std::cerr << "handled error contract failed\n";
        return 3;
    }
    operation->remove_recovery_batch(2, 0);
    operation->add_recovery_words(2, {first.bytes.data(), first.bytes.size()});
    const auto duplicate = operation->add_recovery_words(2, {first.bytes.data(), first.bytes.size()});
    if (duplicate.status != Status::DuplicateShare || duplicate.ready) {
        std::cerr << "duplicate contract failed\n";
        return 4;
    }
    operation->remove_recovery_batch(2, 1);
    const auto ready = operation->add_recovery_words(2, {second.bytes.data(), second.bytes.size()});
    if (ready.status != Status::Ok || !ready.ready) {
        std::cerr << "readiness contract failed\n";
        return 5;
    }
    const auto recovered = operation->recover_words(2);
    if (recovered.status != Status::Ok || stringFrom(recovered.bytes) != exact) {
        std::cerr << "exact recovery contract failed\n";
        return 6;
    }

    destroy_operation(std::move(operation));
    std::cout << "CXX_DESKTOP_BOUNDARY_OK shares=3 threshold=2 encoding=Words handled_errors=yes explicit_release=yes\n";
    return 0;
}
