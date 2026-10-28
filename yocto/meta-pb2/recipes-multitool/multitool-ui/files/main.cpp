/* SPDX-License-Identifier: MIT */
#include "appwindow.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;

constexpr const char *socket_path = "/run/multitool/multitoold.sock";
constexpr const char *openocd_scripts = "/usr/share/openocd/scripts";

constexpr const char *openocd_owner_path = "/run/multitool/openocd-owner";

std::pair<bool, std::string> programmer_owner()
{
	std::ifstream input(openocd_owner_path);
	std::string owner = "local";
	std::string peer = "-";
	input >> owner >> peer;
	return {owner == "remote", peer};
}

std::string request(const char *command)
{
	int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return std::string("Socket: ") + std::strerror(errno);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
	if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
		std::string error = std::string("multitoold: ") + std::strerror(errno);
		::close(fd);
		return error;
	}
	const auto length = std::strlen(command);
	if (::write(fd, command, length) != static_cast<ssize_t>(length)) {
		std::string error = std::string("Write: ") + std::strerror(errno);
		::close(fd);
		return error;
	}
	char response[768];
	const auto received = ::read(fd, response, sizeof(response) - 1);
	::close(fd);
	if (received < 0)
		return std::string("Read: ") + std::strerror(errno);
	response[received] = '\0';
	std::string result(response);
	while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
		result.pop_back();
	return result.empty() ? "No response" : result;
}

slint::SharedString ui_request(const char *command)
{
	const auto result = request(command);
	return slint::SharedString(result.c_str());
}

std::string display_name(std::string name)
{
	for (char &ch : name) {
		if (ch == '_' || ch == '-')
			ch = ' ';
		else
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	}
	return name;
}

CatalogEntry make_entry(const std::string &name, const std::string &path)
{
	CatalogEntry entry{};
	entry.name = slint::SharedString(name.c_str());
	entry.path = slint::SharedString(path.c_str());
	return entry;
}

std::vector<CatalogEntry> scan_openocd_directory(const char *kind)
{
	std::vector<std::pair<std::string, std::string>> found;
	const fs::path directory = fs::path(openocd_scripts) / kind;
	std::error_code error;

	for (fs::directory_iterator it(directory,
		     fs::directory_options::skip_permission_denied, error), end;
	     it != end; it.increment(error)) {
		if (error) {
			error.clear();
			continue;
		}
		if (!it->is_regular_file(error) || it->path().extension() != ".cfg")
			continue;
		found.emplace_back(display_name(it->path().stem().string()),
			(fs::path(kind) / it->path().filename()).generic_string());
	}

	std::sort(found.begin(), found.end());
	std::vector<CatalogEntry> result;
	result.reserve(found.size());
	for (const auto &[name, path] : found)
		result.push_back(make_entry(name, path));
	return result;
}

std::vector<CatalogEntry> scan_profiles()
{
	auto profiles = scan_openocd_directory("board");
	auto targets = scan_openocd_directory("target");
	profiles.insert(profiles.end(), targets.begin(), targets.end());
	return profiles;
}

std::vector<CatalogEntry> scan_hex_files()
{
	static const fs::path roots[] = {
		"/run/media", "/media", "/mnt", "/home/root"
	};
	std::vector<std::pair<std::string, std::string>> found;

	for (const auto &root : roots) {
		std::error_code error;
		if (!fs::is_directory(root, error))
			continue;
		for (fs::recursive_directory_iterator it(root,
			     fs::directory_options::skip_permission_denied, error), end;
		     it != end; it.increment(error)) {
			if (error) {
				error.clear();
				continue;
			}
			if (it.depth() > 3) {
				it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file(error))
				continue;
			auto extension = it->path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (extension != ".hex")
				continue;
			found.emplace_back(it->path().filename().string(),
				it->path().generic_string());
		}
	}

	const std::string readback = "/media/usb/readback.hex";
	const bool has_readback = std::any_of(found.begin(), found.end(),
		[&](const auto &item) { return item.second == readback; });
	if (!has_readback)
		found.emplace_back("readback.hex (new)", readback);

	std::sort(found.begin(), found.end());
	found.erase(std::unique(found.begin(), found.end()), found.end());

	std::vector<CatalogEntry> result;
	result.reserve(found.size());
	for (const auto &[name, path] : found)
		result.push_back(make_entry(name, path));
	return result;
}

template<typename Setter>
void set_catalog(Setter setter, std::vector<CatalogEntry> entries)
{
	if (entries.empty())
		return;
	setter(std::make_shared<slint::VectorModel<CatalogEntry>>(std::move(entries)));
}
}

int main()
{
	auto window = AppWindow::create();

	auto reload_openocd = [window] {
		set_catalog([window](const auto &model) { window->set_mcu_configs(model); },
			scan_openocd_directory("target"));
		set_catalog([window](const auto &model) { window->set_profile_configs(model); },
			scan_profiles());
	};
	auto reload_hex = [window] {
		set_catalog([window](const auto &model) { window->set_hex_files(model); },
			scan_hex_files());
	};

	window->on_refresh([window] {
		window->set_status_text(ui_request("STATUS\n"));
	});
	window->on_reload_openocd_configs(reload_openocd);
	window->on_openocd_config_apply([window](const slint::SharedString &profile, bool swd) {
		const std::string value(profile.data());
		const auto [remote, peer] = programmer_owner();
		const bool prefix_ok = value.rfind("target/", 0) == 0 || value.rfind("board/", 0) == 0;
		const bool suffix_ok = value.size() > 4 && value.substr(value.size() - 4) == ".cfg";
		if (remote) {
			window->set_status_text(slint::SharedString(("Remote owner: " + peer).c_str()));
			return;
		}
		if (!prefix_ok || !suffix_ok || value.find("..") != std::string::npos) {
			window->set_status_text(slint::SharedString("Invalid OpenOCD profile"));
			return;
		}
		std::ofstream output("/etc/multitool/openocd-remote.conf.tmp", std::ios::trunc);
		output << "PROFILE=" << value << "\nTRANSPORT=" << (swd ? "swd" : "jtag") << "\n";
		output.close();
		std::error_code error;
		fs::rename("/etc/multitool/openocd-remote.conf.tmp",
			"/etc/multitool/openocd-remote.conf", error);
		window->set_status_text(slint::SharedString(error
			? ("Config: " + error.message()).c_str()
			: "OpenOCD profile saved"));
	});
	window->on_browse_image(reload_hex);
	window->on_start_pru([window] {
		window->set_status_text(ui_request("PRU START\n"));
	});
	window->on_scope_start([window] {
		window->set_status_text(ui_request("SCOPE START ads8688 - ff 262144\n"));
	});
	window->on_scope_stop([window] {
		window->set_status_text(ui_request("SCOPE STOP\n"));
	});

	reload_openocd();
	reload_hex();
	window->set_status_text(ui_request("STATUS\n"));
	slint::Timer owner_timer;
	auto refresh_owner = [window] {
		const auto [remote, peer] = programmer_owner();
		window->set_programmer_remote(remote);
		window->set_programmer_owner_text(remote
			? slint::SharedString(("REMOTE " + peer).c_str())
			: slint::SharedString("LOCAL SCREEN"));
		if (remote) {
			window->set_program_menu_open(false);
			window->set_status_text(slint::SharedString(
				("Remote OpenOCD session from " + peer).c_str()));
		}
	};
	refresh_owner();
	owner_timer.start(slint::TimerMode::Repeated,
		std::chrono::milliseconds(500), refresh_owner);
	window->run();
	return 0;
}
