param([Parameter(Mandatory=$true)][string]$Source)
$ErrorActionPreference = 'Stop'
$encoding = New-Object Text.UTF8Encoding $false
function Replace-Once([string]$Text,[string]$Old,[string]$New) {
    $Old = $Old.Replace("`r`n","`n")
    $New = $New.Replace("`r`n","`n")
    $first = $Text.IndexOf($Old,[StringComparison]::Ordinal)
    if ($first -lt 0 -or $Text.IndexOf($Old,$first+$Old.Length,[StringComparison]::Ordinal) -ge 0) {
        throw 'Pinned librime source differs from the reviewed storage adapter. No heuristic patch was applied.'
    }
    return $Text.Replace($Old,$New)
}
$header = Join-Path $Source 'src/rime/dict/user_dictionary.h'
$text = [IO.File]::ReadAllText($header).Replace("`r`n","`n")
$text = Replace-Once $text '  bool Initialize();' @'
  // Product storage wrapper delegates both scopes to the upstream algorithm.
  bool MyimeUpdateEntry(const DictEntry& entry, int commits,
                        const string& new_entry_prefix);
  bool Initialize();
'@
[IO.File]::WriteAllText($header,$text,$encoding)
$implementation = Join-Path $Source 'src/rime/dict/user_dictionary.cc'
$text = [IO.File]::ReadAllText($implementation).Replace("`r`n","`n")
$text = Replace-Once $text '#include <rime/dict/user_dictionary.h>' @'
#include <rime/dict/user_dictionary.h>
#define MYIME_STORAGE_BUILD
#include <myime_storage/rime_storage.h>
'@
$old = @'
                                 const string& new_entry_prefix) {
  string code_str(entry.custom_code);
'@
$replacement = @'
                                 const string& new_entry_prefix) {
  // Upstream Memory may still notify an unloaded user dictionary on commit.
  // Optional shared storage must never dereference a missing/closed database.
  if (!loaded() || readonly()) return false;
  if (!myime_storage::active(db_.get()))
    return MyimeUpdateEntry(entry, commits, new_entry_prefix);
  myime_storage::WriteGuard transaction(db_.get());
  if (!transaction) return false;
  bool global = false, grouped = false;
  {
    myime_storage::Scope scope(0);
    if (FetchTickCount() || Initialize())
      global = MyimeUpdateEntry(entry, commits, new_entry_prefix);
  }
  {
    myime_storage::Scope scope(1);
    if (FetchTickCount() || Initialize())
      grouped = MyimeUpdateEntry(entry, commits, new_entry_prefix);
  }
  transaction.complete(global && grouped);
  FetchTickCount();
  return global && grouped;
}

bool UserDictionary::MyimeUpdateEntry(const DictEntry& entry,
                                      int commits,
                                      const string& new_entry_prefix) {
  string code_str(entry.custom_code);
'@
$text = Replace-Once $text $old $replacement
$old = @'
                                   string* resume_key) {
  TickCount present_tick = tick_ + 1;
'@
$replacement = @'
                                   string* resume_key) {
  if (!loaded()) return 0;
  if (myime_storage::active(db_.get())) FetchTickCount();
  TickCount present_tick = tick_ + 1;
'@
$text = Replace-Once $text $old $replacement
$text = Replace-Once $text '  auto db = db_pool_[dict_name].lock();' @'
  const auto pool_key = myime_storage::pool_key(dict_name, db_class);
  auto db = db_pool_[pool_key].lock();
'@
$text = Replace-Once $text '    db_pool_[dict_name] = db;' '    db_pool_[pool_key] = db;'
[IO.File]::WriteAllText($implementation,$text,$encoding)
$cmake = Join-Path $Source 'CMakeLists.txt'
$text = [IO.File]::ReadAllText($cmake).Replace("`r`n","`n")
$text = Replace-Once $text '  find_package(Boost 1.77.0)' '  find_package(Boost 1.77.0 CONFIG REQUIRED COMPONENTS regex)'
$text = Replace-Once $text '  add_definitions(-DBOOST_DLL_USE_STD_FS)' '  add_definitions(-DBOOST_DLL_USE_STD_FS -DBOOST_ALL_NO_LIB)'
[IO.File]::WriteAllText($cmake,$text,$encoding)
