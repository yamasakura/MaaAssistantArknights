#include "RoguelikeCopilotConfig.h"

#include <meojson/json.hpp>

#include "Utils/Logger.hpp"

using namespace asst::battle;
using namespace asst::battle::roguelike;

std::optional<CombatData> asst::RoguelikeCopilotConfig::get_stage_data(const std::string& stage_name) const
{
    auto it = m_stage_data.find(stage_name);
    if (it == m_stage_data.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool asst::RoguelikeCopilotConfig::parse(const json::value& json)
{
    for (const auto& stage_info : json.as_array()) {
        std::string stage_name = stage_info.at("stage_name").as_string();
        CombatData data;
        data.stage_name = stage_name;
        static const std::unordered_map<std::string, DeployDirection> DeployDirectionMapping = {
            { "Right", DeployDirection::Right }, { "RIGHT", DeployDirection::Right },
            { "right", DeployDirection::Right }, { "右", DeployDirection::Right },

            { "Left", DeployDirection::Left },   { "LEFT", DeployDirection::Left },
            { "left", DeployDirection::Left },   { "左", DeployDirection::Left },

            { "Up", DeployDirection::Up },       { "UP", DeployDirection::Up },
            { "up", DeployDirection::Up },       { "上", DeployDirection::Up },

            { "Down", DeployDirection::Down },   { "DOWN", DeployDirection::Down },
            { "down", DeployDirection::Down },   { "下", DeployDirection::Down },

            { "None", DeployDirection::None },   { "NONE", DeployDirection::None },
            { "none", DeployDirection::None },   { "无", DeployDirection::None },
        };
        if (auto opt = stage_info.find<json::array>("replacement_home")) {
            for (auto& point : opt.value()) {
                ReplacementHome home;
                home.location = Point(point["location"][0].as_integer(), point["location"][1].as_integer());
                const std::string& direction_str = point.get("direction", "none");
                if (auto iter = DeployDirectionMapping.find(direction_str); iter != DeployDirectionMapping.end()) {
                    home.direction = iter->second;
                }
                else {
                    home.direction = DeployDirection::None;
                }
                data.replacement_home.emplace_back(std::move(home));
            }
        }
        if (auto opt = stage_info.find<json::array>("blacklist_location")) {
            for (auto& point : opt.value()) {
                data.blacklist_location.emplace(point[0].as_integer(), point[1].as_integer());
            }
        }
        data.use_dice_stage = !stage_info.get("not_use_dice", false);

        if (auto opt = stage_info.find<json::value>("force_air_defense_when_deploy_blocking_num")) {
            data.stop_deploy_blocking_num = opt.value().get("melee_num", INT_MAX);
            data.force_deploy_air_defense_num = opt.value().get("air_defense_num", 0);
            if (data.force_deploy_air_defense_num == 0) {
                data.stop_deploy_blocking_num = INT_MAX;
            }
            data.force_ban_medic = opt.value().get("ban_medic", false);
        }
        else {
            data.stop_deploy_blocking_num = INT_MAX;
            data.force_deploy_air_defense_num = 0;
            data.force_ban_medic = false;
        }

        constexpr int RoleNumber = 9;
        static constexpr std::array<Role, RoleNumber> RoleOrder = {
            Role::Warrior, Role::Pioneer, Role::Medic,   Role::Tank,  Role::Sniper,
            Role::Caster,  Role::Support, Role::Special, Role::Drone,
        };

        if (auto opt = stage_info.find<json::array>("role_order")) {
            const auto& raw_roles = opt.value();
            using views::filter, views::transform;
            std::unordered_set<Role> specified_role;
            std::vector<Role> role_order;
            bool is_legal = true;
            if (ranges::find_if_not(raw_roles | views::all, std::mem_fn(&json::value::is_string)) != raw_roles.end()) {
                Log.error("Role should be string");
                return false;
            }
            auto roles = raw_roles | filter(&json::value::is_string) | transform(&json::value::as_string) |
                         transform([&](std::string name) {
                             utils::tolowers(name);
                             return name;
                         });
            for (const std::string& role_name : roles) {
                const auto role = get_role_type(role_name);
                if (role == Role::Unknown) [[unlikely]] {
                    Log.error("Unknown Role:", role_name);
                    is_legal = false;
                    break;
                }
                if (specified_role.contains(role)) [[unlikely]] {
                    Log.error("Duplicated Role:", role_name);
                    is_legal = false;
                    break;
                }
                specified_role.emplace(role);
                role_order.emplace_back(role);
            }
            if (is_legal) [[likely]] {
                ranges::copy(RoleOrder | filter([&](Role role) { return !specified_role.contains(role); }),
                             std::back_inserter(role_order));
                if (role_order.size() != RoleNumber) [[unlikely]] {
                    Log.error("Unexpected role_order size:", role_order.size());
                    return false;
                }
                ranges::move(role_order, data.role_order.begin());
            }
            else {
                Log.error("Illegal role_order detected");
                return false;
            }
        }
        else {
            data.role_order = RoleOrder;
        }

        if (auto opt = stage_info.find<json::array>("force_deploy_direction")) {
            for (auto& point : opt.value()) {
                ForceDeployDirection fd_dir;
                Point location = Point(point["location"][0].as_integer(), point["location"][1].as_integer());
                const std::string& direction_str = point.get("direction", "none");
                if (auto iter = DeployDirectionMapping.find(direction_str); iter != DeployDirectionMapping.end()) {
                    fd_dir.direction = iter->second;
                }
                else {
                    fd_dir.direction = DeployDirection::None;
                }
                if (fd_dir.direction == DeployDirection::None) [[unlikely]] {
                    Log.error("Unknown direction");
                    return false;
                }
                std::unordered_set<Role> fd_role;
                for (auto& role_name : point["role"].as_array()) {
                    const auto role = get_role_type(role_name.as_string());
                    if (role == Role::Unknown) [[unlikely]] {
                        Log.error("Unknown role name:", role_name);
                        return false;
                    }
                    fd_role.emplace(role);
                }
                fd_dir.role = std::move(fd_role);
                data.force_deploy_direction.emplace(location, fd_dir);
            }
        }

        m_stage_data.emplace(std::move(stage_name), std::move(data));
    }
    return true;
}
