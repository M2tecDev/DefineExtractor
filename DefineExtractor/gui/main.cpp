// main.cpp — DefineExtractor GUI v2.0

#include "main_window.h"
#include "../library.h"

#include <slint.h>
#include <private/slint_models.h>

#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <iostream>
#include <memory>
#include <algorithm>
#include <ranges>
#include <fstream>
#include <chrono>
#include <unordered_set>
#include <set>

// Windows native folder picker
#include <shobjidl.h>
#include <windows.h>

using UiHandle = slint::ComponentHandle<MainWindow>;
using WeakUiHandle = slint::ComponentWeakHandle<MainWindow>;

struct AppState {
    std::string client_path;
    std::string server_path;
    std::string python_root;
    std::string client_header;
    std::string server_header;
    bool client_set = false;
    bool server_set = false;
    bool python_set = false;
    std::vector<std::string> client_defines;
    std::vector<std::string> server_defines;
    std::vector<bool> client_defines_selected;
    std::vector<bool> server_defines_selected;
    std::vector<size_t> filtered_client_indices;
    std::vector<size_t> filtered_server_indices;
    std::string client_defines_filter;
    std::string server_defines_filter;
    std::vector<std::string> params;
    std::vector<std::string> params_names;       // full unfiltered list
    std::vector<bool>        params_selected;    // parallel to params_names
    std::vector<size_t>      filtered_indices;   // indices into params_names matching current filter
    std::string              params_filter;
    int params_count = 0;
    int params_selected_count = 0;
    WeakUiHandle ui;
    std::string changelog_text;
    std::vector<std::string> recent_folders;
    std::vector<int> recent_timestamps;
};

static AppState g_state;

static std::string pick_folder_native() {
    std::string result;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool com_initialized = SUCCEEDED(hr);
    IFileDialog* pDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
    if (SUCCEEDED(hr) && pDialog) {
        DWORD dwOptions;
        pDialog->GetOptions(&dwOptions);
        pDialog->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        pDialog->SetTitle(L"Select Directory");
        HWND hwnd = GetActiveWindow();
        hr = pDialog->Show(hwnd);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pDialog->GetResult(&pItem);
            if (SUCCEEDED(hr) && pItem) {
                wchar_t* pwszPath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pwszPath);
                if (SUCCEEDED(hr) && pwszPath) {
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, pwszPath, -1, nullptr, 0, nullptr, nullptr);
                    if (size_needed > 0) {
                        result.resize(size_needed);
                        WideCharToMultiByte(CP_UTF8, 0, pwszPath, -1, &result[0], size_needed, nullptr, nullptr);
                        result.pop_back();
                    }
                    CoTaskMemFree(pwszPath);
                }
                pItem->Release();
            }
        }
        pDialog->Release();
    }
    if (com_initialized) CoUninitialize();
    return result;
}


static void append_log(const std::string& message) {
    slint::invoke_from_event_loop([weakUi = g_state.ui, message]() {
        if (const auto ui = weakUi.lock()) {
            std::string text = (*ui)->get_log_text().data();
            text += message;
            text += '\n';
            (*ui)->set_log_text(slint::SharedString(text));
        }
    });
}

static void log_file_issues(const std::vector<extraction::FileIssue>& issues) {
    if (issues.empty()) return;
    std::string message;
    for (const auto& issue : issues) {
        if (!message.empty()) message += '\n';
        message += std::format("Skipped {}: {}", extraction::pathText(issue.path), issue.message);
    }
    append_log(message);
}

static void start_extraction(const UiHandle& app, std::jthread& worker, fs::path root,
                             std::vector<std::string> symbols, extraction::Language language,
                             std::string prefix) {
    if (app->get_is_running()) return;
    const auto threads = app->get_opt_multithread()
        ? std::max(1u, std::thread::hardware_concurrency() / 2) : 1u;
    app->set_is_running(true);
    app->set_progress_value(0.0f);
    app->set_status_text(slint::SharedString("Finding source files..."));
    const auto weakUi = g_state.ui;
    try {
        worker = std::jthread([weakUi, root = std::move(root), symbols = std::move(symbols),
                               language, prefix = std::move(prefix), threads] {
            std::string status;
            try {
                auto files = findSourceFiles(root, language);
                log_file_issues(files.issues);
                if (files.paths.empty()) {
                    status = "No source files found";
                    append_log(status + " in " + extraction::pathText(root));
                } else {
                    const auto progress = [weakUi](size_t completed, size_t total) {
                        slint::invoke_from_event_loop([weakUi, completed, total] {
                            if (const auto ui = weakUi.lock()) {
                                (*ui)->set_progress_value(static_cast<float>(completed) / static_cast<float>(total));
                                (*ui)->set_status_text(slint::SharedString(
                                    std::format("Scanning {}/{} files", completed, total)));
                            }
                        });
                    };
                    const auto result = extraction::scan(std::move(files.paths), symbols, language, threads, progress);
                    log_file_issues(result.issues);
                    const auto failures = files.issues.size() + result.issues.size();
                    status = std::format("Processed: {} | Read or discovery errors: {}",
                                         result.processed, failures);
                    append_log(status);
                    if (result.processed != 0) {
                        const auto output = extraction::exportRun(result, root, prefix);
                        std::string report;
                        for (const auto& [symbol, hits] : result.symbols) {
                            report += std::format("{}: {} conditions, {} functions\n",
                                                  symbol, hits.conditions.size(), hits.functions.size());
                        }
                        report += "Results: " + extraction::pathText(output);
                        append_log(report);
                    }
                }
            } catch (const std::exception& error) {
                status = "Extraction failed";
                append_log(status + ": " + error.what());
            }
            slint::invoke_from_event_loop([weakUi, status = std::move(status)] {
                if (const auto ui = weakUi.lock()) {
                    (*ui)->set_is_running(false);
                    (*ui)->set_progress_value(0.0f);
                    (*ui)->set_status_text(slint::SharedString(status));
                }
            });
        });
    } catch (const std::exception& error) {
        app->set_is_running(false);
        app->set_status_text(slint::SharedString("Could not start extraction"));
        append_log(error.what());
    }
}

static void update_params_display(const UiHandle& app) {
    const std::string& filter = g_state.params_filter;
    g_state.filtered_indices.clear();
    for (size_t i = 0; i < g_state.params_names.size(); i++) {
        if (filter.empty() ||
            g_state.params_names[i].find(filter) != std::string::npos)
            g_state.filtered_indices.push_back(i);
    }

    std::vector<slint::SharedString> names_vec;
    std::vector<bool> sel_vec;
    names_vec.reserve(g_state.filtered_indices.size());
    sel_vec.reserve(g_state.filtered_indices.size());
    for (size_t fi : g_state.filtered_indices) {
        names_vec.push_back(slint::SharedString(g_state.params_names[fi]));
        sel_vec.push_back(g_state.params_selected[fi]);
    }

    int sel_count = (int)std::count(g_state.params_selected.begin(),
                                    g_state.params_selected.end(), true);
    g_state.params_selected_count = sel_count;
    g_state.params_count = (int)g_state.filtered_indices.size();

    app->set_params_names(std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(names_vec)));
    app->set_params_selected(std::make_shared<slint::VectorModel<bool>>(std::move(sel_vec)));
    app->set_params_count(g_state.params_count);
    app->set_params_selected_count(sel_count);
}

static void update_client_defines_display(const UiHandle& app) {
    const std::string& filter = g_state.client_defines_filter;
    g_state.filtered_client_indices.clear();
    for (size_t i = 0; i < g_state.client_defines.size(); i++) {
        if (filter.empty() || g_state.client_defines[i].find(filter) != std::string::npos)
            g_state.filtered_client_indices.push_back(i);
    }
    std::vector<slint::SharedString> names;
    std::vector<bool> sel;
    names.reserve(g_state.filtered_client_indices.size());
    sel.reserve(g_state.filtered_client_indices.size());
    for (size_t fi : g_state.filtered_client_indices) {
        names.push_back(slint::SharedString(g_state.client_defines[fi]));
        sel.push_back(g_state.client_defines_selected[fi]);
    }
    int sel_count = (int)std::count(g_state.client_defines_selected.begin(),
                                    g_state.client_defines_selected.end(), true);
    app->set_defines_names(std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(names)));
    app->set_defines_selected(std::make_shared<slint::VectorModel<bool>>(std::move(sel)));
    app->set_defines_count((int)g_state.filtered_client_indices.size());
    app->set_defines_selected_count(sel_count);
}

static void update_server_defines_display(const UiHandle& app) {
    const std::string& filter = g_state.server_defines_filter;
    g_state.filtered_server_indices.clear();
    for (size_t i = 0; i < g_state.server_defines.size(); i++) {
        if (filter.empty() || g_state.server_defines[i].find(filter) != std::string::npos)
            g_state.filtered_server_indices.push_back(i);
    }
    std::vector<slint::SharedString> names;
    std::vector<bool> sel;
    names.reserve(g_state.filtered_server_indices.size());
    sel.reserve(g_state.filtered_server_indices.size());
    for (size_t fi : g_state.filtered_server_indices) {
        names.push_back(slint::SharedString(g_state.server_defines[fi]));
        sel.push_back(g_state.server_defines_selected[fi]);
    }
    int sel_count = (int)std::count(g_state.server_defines_selected.begin(),
                                    g_state.server_defines_selected.end(), true);
    app->set_server_defines_names(std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(names)));
    app->set_server_defines_selected(std::make_shared<slint::VectorModel<bool>>(std::move(sel)));
    app->set_server_defines_count((int)g_state.filtered_server_indices.size());
    app->set_server_defines_selected_count(sel_count);
}

static void set_python_root(const UiHandle& ui, std::string root) {
    if (root != g_state.python_root) {
        g_state.params_names.clear();
        g_state.params_selected.clear();
        g_state.params_filter.clear();
        update_params_display(ui);
    }
    g_state.python_root = std::move(root);
    g_state.python_set = !g_state.python_root.empty();
    ui->set_python_root(slint::SharedString(g_state.python_root));
    ui->set_python_set(g_state.python_set);
}

static void on_open_recent_folder(const UiHandle& ui, const std::string& path) {
    if (ui->get_is_running()) return;
    const auto chosen = extraction::pathFromText(path);
    bool has_header = false;
    std::string header_name;
    findClientHeaderInUserInterface(chosen, has_header, header_name);
    if (has_header) {
        g_state.client_path = path;
        g_state.client_header = header_name;
        g_state.client_set = true;
        ui->set_client_path(slint::SharedString(path));
        ui->set_client_set(true);
    } else {
        findServerHeaderInCommon(chosen, has_header, header_name);
        if (has_header) {
            g_state.server_path = path;
            g_state.server_header = header_name;
            g_state.server_set = true;
            ui->set_server_path(slint::SharedString(path));
            ui->set_server_set(true);
        } else {
            std::vector<std::string> py_roots;
            findPythonRoots(chosen, py_roots);
            if (!py_roots.empty()) {
                set_python_root(ui, std::move(py_roots.front()));
            }
        }
    }
    g_state.recent_folders.insert(g_state.recent_folders.begin(), path);
    if (g_state.recent_folders.size() > 10) g_state.recent_folders.resize(10);
    std::vector<slint::SharedString> folders;
    for (const auto& f : g_state.recent_folders) folders.push_back(slint::SharedString(f));
    ui->set_recent_folders(std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(folders)));
}

static void on_load_defines(const UiHandle& ui, int mode) {
    std::string header_path;
    if (mode == 0) {
        if (!g_state.client_set) return;
        header_path = g_state.client_header;
    } else {
        if (!g_state.server_set) return;
        header_path = g_state.server_header;
    }
    std::vector<std::string> defs;
    try {
        defs = readDefines(header_path);
    } catch (const std::exception& error) {
        append_log(std::format("Could not read {}: {}", header_path, error.what()));
        return;
    }
    if (mode == 0) {
        g_state.client_defines = defs;
        g_state.client_defines_selected.assign(defs.size(), false);
        g_state.client_defines_filter.clear();
        update_client_defines_display(ui);
    } else {
        g_state.server_defines = defs;
        g_state.server_defines_selected.assign(defs.size(), false);
        g_state.server_defines_filter.clear();
        update_server_defines_display(ui);
    }
    append_log("Loaded " + std::to_string(defs.size()) + " defines");
}

int main() {
    std::cout << "DefineExtractor GUI v2.0 starting..." << std::endl;
    auto app = MainWindow::create();
    g_state.ui = WeakUiHandle(app);
    std::jthread worker;

    // Initialize recent folders (could be loaded from config in future)
    std::vector<slint::SharedString> init_folders;
    app->set_recent_folders(std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(init_folders)));

    // Set changelog
    app->set_changelog_text(slint::SharedString("Welcome to DefineExtractor v2.0!\n\nUse the sidebar to navigate between pages.\n\n• Home - Overview and recent folders\n• Defines - Load and extract C++ defines from headers\n• Params - Extract Python parameters from code\n• Settings - Configure paths for Client, Server, Python\n• Logs - View output logs"));

    // Connect all callbacks
    app->on_open_recent_folder([app](slint::SharedString p) { on_open_recent_folder(app, p.data() ? std::string(p.data()) : std::string()); });
    app->on_load_defines_callback([app](int m) { on_load_defines(app, m); });
    app->on_browse_python_callback([app]() {
        std::string selected_path = pick_folder_native();
        if (selected_path.empty()) return;
        const auto chosen = extraction::pathFromText(selected_path);
        std::vector<std::string> py_roots;
        findPythonRoots(chosen, py_roots);
        set_python_root(app, py_roots.empty() ? selected_path : std::move(py_roots.front()));
        append_log("Python root set: " + g_state.python_root);
    });
    app->on_load_python_params_callback([&app, &worker]() {
        if (!g_state.python_set) {
            append_log("Python root not set!");
            return;
        }
        if (app->get_is_running()) return;
        app->set_is_running(true);
        app->set_status_text(slint::SharedString("Finding Python parameters..."));
        try {
            worker = std::jthread([weakUi = g_state.ui, root = g_state.python_root] {
                std::vector<std::string> names;
                std::string status;
                bool success = false;
                try {
                    auto files = findSourceFiles(extraction::pathFromText(root), extraction::Language::Python);
                    auto found = extraction::collectPythonParameters(files.paths, files.issues);
                    names.assign(found.begin(), found.end());
                    std::ranges::sort(names);
                    log_file_issues(files.issues);
                    status = std::format("Found {} Python parameters in {} files", names.size(),
                                         files.paths.size());
                    append_log(status);
                    success = true;
                } catch (const std::exception& error) {
                    status = "Could not load Python parameters";
                    append_log(status + ": " + error.what());
                }
                slint::invoke_from_event_loop([weakUi, root, names = std::move(names),
                                               status = std::move(status), success]() mutable {
                    if (const auto ui = weakUi.lock()) {
                        if (success && root == g_state.python_root) {
                            g_state.params_names = std::move(names);
                            g_state.params_selected.assign(g_state.params_names.size(), false);
                            g_state.params_filter.clear();
                            update_params_display(*ui);
                        }
                        (*ui)->set_status_text(slint::SharedString(status));
                        (*ui)->set_is_running(false);
                    }
                });
            });
        } catch (const std::exception& error) {
            app->set_is_running(false);
            app->set_status_text(slint::SharedString("Could not load Python parameters"));
            append_log(error.what());
        }
    });
    app->on_debug_log([]() {
        std::cout << "DEBUG: debug_log called!" << std::endl;
    });
    app->on_select_all_params_callback([app](bool y) {
        // operate on the full list, then refresh display
        std::fill(g_state.params_selected.begin(), g_state.params_selected.end(), y);
        update_params_display(app);
    });
    app->on_toggle_param_callback([app](int display_i) {
        if (display_i < 0 || display_i >= (int)g_state.filtered_indices.size()) return;
        size_t orig = g_state.filtered_indices[display_i];
        g_state.params_selected[orig] = !g_state.params_selected[orig];
        update_params_display(app);
    });
    app->on_filter_params_callback([app](slint::SharedString filter) {
        g_state.params_filter = filter.data() ? std::string(filter.data()) : "";
        update_params_display(app);
    });
    app->on_run_python_extraction_callback([&app, &worker]() {
        if (!g_state.python_set || g_state.params_selected_count == 0) {
            append_log("No params selected for extraction");
            return;
        }
        std::vector<std::string> symbols;
        for (size_t index = 0; index < g_state.params_selected.size(); ++index) {
            if (g_state.params_selected[index]) symbols.push_back(g_state.params_names[index]);
        }
        start_extraction(app, worker, extraction::pathFromText(g_state.python_root), std::move(symbols),
                         extraction::Language::Python, "PYTHON");
    });
    app->on_browse_path_callback([app](int mode) {
        std::string selected_path = pick_folder_native();
        if (selected_path.empty()) return;
        const auto chosen = extraction::pathFromText(selected_path);
        bool has_header = false;
        std::string header_name;
        if (mode == 0) {
            g_state.client_path = selected_path;
            findClientHeaderInUserInterface(chosen, has_header, header_name);
            g_state.client_set = has_header;
            g_state.client_header = header_name;
            app->set_client_path(slint::SharedString(g_state.client_path));
            app->set_client_set(has_header);
            append_log("Client path: " + g_state.client_path);
            g_state.recent_folders.insert(g_state.recent_folders.begin(), selected_path);
        } else if (mode == 1) {
            g_state.server_path = selected_path;
            findServerHeaderInCommon(chosen, has_header, header_name);
            g_state.server_set = has_header;
            g_state.server_header = header_name;
            app->set_server_path(slint::SharedString(g_state.server_path));
            app->set_server_set(has_header);
            append_log("Server path: " + g_state.server_path);
            g_state.recent_folders.insert(g_state.recent_folders.begin(), selected_path);
        } else {
            std::vector<std::string> py_roots;
            findPythonRoots(chosen, py_roots);
            if (!py_roots.empty()) {
                set_python_root(app, std::move(py_roots.front()));
                append_log("Python root: " + g_state.python_root);
                g_state.recent_folders.insert(g_state.recent_folders.begin(), selected_path);
            }
        }
        if (g_state.recent_folders.size() > 10) g_state.recent_folders.resize(10);
        std::vector<slint::SharedString> folders;
        for (const auto& f : g_state.recent_folders) folders.push_back(slint::SharedString(f));
        app->set_recent_folders(std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(folders)));
    });
    app->on_clear_paths_callback([app]() {
        if (app->get_is_running()) return;
        g_state.client_path.clear();
        g_state.server_path.clear();
        g_state.client_header.clear();
        g_state.server_header.clear();
        g_state.client_set = false;
        g_state.server_set = false;
        g_state.client_defines.clear();
        g_state.client_defines_selected.clear();
        g_state.server_defines.clear();
        g_state.server_defines_selected.clear();
        set_python_root(app, {});
        app->set_client_path(slint::SharedString(""));
        app->set_server_path(slint::SharedString(""));
        app->set_client_set(false);
        app->set_server_set(false);
        app->set_defines_count(0);
        app->set_defines_selected_count(0);
        app->set_server_defines_count(0);
        app->set_server_defines_selected_count(0);
    });
    app->on_clear_log_callback([app]() {
        app->set_log_text(slint::SharedString(""));
    });
    app->on_select_all_defines_callback([app](bool y) {
        std::fill(g_state.client_defines_selected.begin(), g_state.client_defines_selected.end(), y);
        update_client_defines_display(app);
    });
    app->on_toggle_defines_callback([app](int display_i) {
        if (display_i < 0 || display_i >= (int)g_state.filtered_client_indices.size()) return;
        size_t orig = g_state.filtered_client_indices[display_i];
        g_state.client_defines_selected[orig] = !g_state.client_defines_selected[orig];
        update_client_defines_display(app);
    });
    app->on_select_all_server_defines_callback([app](bool y) {
        std::fill(g_state.server_defines_selected.begin(), g_state.server_defines_selected.end(), y);
        update_server_defines_display(app);
    });
    app->on_toggle_server_defines_callback([app](int display_i) {
        if (display_i < 0 || display_i >= (int)g_state.filtered_server_indices.size()) return;
        size_t orig = g_state.filtered_server_indices[display_i];
        g_state.server_defines_selected[orig] = !g_state.server_defines_selected[orig];
        update_server_defines_display(app);
    });
    app->on_client_filter_defines_callback([app](slint::SharedString filter) {
        g_state.client_defines_filter = filter.data() ? std::string(filter.data()) : "";
        update_client_defines_display(app);
    });
    app->on_server_filter_defines_callback([app](slint::SharedString filter) {
        g_state.server_defines_filter = filter.data() ? std::string(filter.data()) : "";
        update_server_defines_display(app);
    });
    app->on_run_extraction_callback([&app, &worker](int mode) {
        const bool client = mode == 0;
        const auto& names = client ? g_state.client_defines : g_state.server_defines;
        const auto& selected = client ? g_state.client_defines_selected : g_state.server_defines_selected;
        const auto& path = client ? g_state.client_path : g_state.server_path;
        if (!(client ? g_state.client_set : g_state.server_set)) {
            append_log("Source path is not set");
            return;
        }
        std::vector<std::string> symbols;
        for (size_t index = 0; index < selected.size(); ++index) {
            if (selected[index]) symbols.push_back(names[index]);
        }
        if (symbols.empty()) {
            append_log("No defines selected for extraction");
            return;
        }
        start_extraction(app, worker, extraction::pathFromText(path), std::move(symbols),
                         extraction::Language::Cpp, client ? "CLIENT" : "SERVER");
    });
    app->on_save_log_callback([app]() {
        std::string log = app->get_log_text().data();
        if (log.empty()) return;
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::string filename = "DefineExtractor_log_" + std::to_string(ms) + ".txt";
        std::ofstream file(filename);
        if (file.is_open()) {
            file << log;
            file.close();
            append_log("Log saved: " + filename);
        }
    });

    app->run();
    return 0;
}
