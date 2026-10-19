// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/drivers/drivers.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "leash/drivers/driver.hpp"

namespace leash::drivers {

std::string_view module_name() { return "drivers"; }

std::string_view driver_name(std::string_view driver_id) noexcept {
    const std::size_t slash = driver_id.find('/');
    if (slash == std::string_view::npos) {
        return driver_id;
    }
    std::string_view name = driver_id;
    name.remove_suffix(driver_id.size() - slash);
    return name;
}

void DriverRegistry::add(std::shared_ptr<Driver> driver) {
    if (!driver) {
        return;
    }
    std::string name(driver_name(driver->id()));
    if (name.empty()) {
        return;
    }
    add(std::move(name), std::move(driver));
}

void DriverRegistry::add(std::string name, std::shared_ptr<Driver> driver) {
    if (name.empty() || !driver) {
        return;
    }
    drivers_.insert_or_assign(std::move(name), std::move(driver));
}

std::shared_ptr<Driver> DriverRegistry::find(std::string_view name) const {
    const auto entry = drivers_.find(name);
    return entry == drivers_.end() ? nullptr : entry->second;
}

bool DriverRegistry::contains(std::string_view name) const noexcept {
    return drivers_.contains(name);
}

std::vector<std::string> DriverRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(drivers_.size());
    for (const auto& entry : drivers_) {
        out.push_back(entry.first);
    }
    return out;
}

std::size_t DriverRegistry::size() const noexcept { return drivers_.size(); }

} // namespace leash::drivers
