#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

// Windows resolves physical directories. Publishing/deploying is an explicit
// offline action; activation only reads a tiny immutable-generation selector.
namespace WorkspacePaths {
inline bool valid_name(const std::string& id,size_t maximum) {
    if (id.empty() || id.size()>maximum) return false;
    for (unsigned char c:id) if (!(c>='a' && c<='z') && !(c>='0' && c<='9') && c!='-' && c!='_') return false;
    return true;
}
inline bool valid_id(const std::string& id) { return valid_name(id,64); }
inline std::string selector(const std::filesystem::path& root) {
    const auto file=root/L"rime"/L"active-workspace.txt";
    if (!std::filesystem::exists(file)) return {};
    std::ifstream input(file,std::ios::binary); if (!input) throw std::runtime_error("Cannot read Rime workspace selector");
    char bytes[130]{}; input.read(bytes,sizeof(bytes)); const auto count=input.gcount();
    if (count>=static_cast<std::streamsize>(sizeof(bytes))) throw std::runtime_error("Rime workspace selector too large");
    std::string id(bytes,static_cast<size_t>(count));
    while (!id.empty() && (id.back()=='\n' || id.back()=='\r')) id.pop_back();
    if (!valid_id(id)) throw std::runtime_error("Invalid Rime workspace selector");
    return id;
}
inline std::filesystem::path shared(const std::filesystem::path& module_directory,
                                    const std::filesystem::path& root) {
    const auto id=selector(root);
    if (id.empty() || id=="base") return module_directory/L"data"/L"shared";
    const auto catalog=std::filesystem::canonical(root/L"rime"/L"workspaces");
    const auto workspace=std::filesystem::canonical(catalog/id);
    if (workspace.parent_path()!=catalog || !std::filesystem::is_regular_file(workspace/L"ready"))
        throw std::runtime_error("Rime workspace is not a published generation");
    const auto result=std::filesystem::canonical(workspace/L"shared");
    if (result.parent_path()!=workspace || !std::filesystem::is_regular_file(result/L"build"/L"pinyin_simp.table.bin"))
        throw std::runtime_error("Rime workspace is incomplete");
    return result;
}
}
