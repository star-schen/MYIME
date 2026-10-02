//! Bounded text import and data-only Rime dictionary packages.
//! Importers never open a Rime session, userdb or schema. No spelling is inferred.
#![forbid(unsafe_code)]

use crate::extensions::{Converter, ImportedWord, Importer};
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    fmt,
    fs::{self, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
};

pub const MAX_INPUT_BYTES: usize = 16 * 1024 * 1024;
pub const MAX_ROWS: usize = 100_000;
pub const MAX_SAMPLE_ROWS: usize = 100;
const MAX_RECORD_BYTES: usize = 32 * 1024 * 1024;
const MAX_PACKAGE_FILE_BYTES: usize = 64 * 1024 * 1024;

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Format {
    Txt,
    Csv,
}
impl Format {
    pub fn parse(value: &str) -> Result<Self, ImportError> {
        match value {
            "txt" => Ok(Self::Txt),
            "csv" => Ok(Self::Csv),
            _ => Err(ImportError::new(
                "format",
                None,
                "format must be txt or csv",
            )),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct ImportError {
    pub kind: String,
    pub line: Option<usize>,
    pub message: String,
}
impl ImportError {
    pub fn new(kind: &str, line: Option<usize>, message: impl Into<String>) -> Self {
        Self {
            kind: kind.into(),
            line,
            message: message.into(),
        }
    }
}
impl fmt::Display for ImportError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some(line) = self.line {
            write!(f, "line {line}: {}", self.message)
        } else {
            self.message.fmt(f)
        }
    }
}
impl std::error::Error for ImportError {}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct Word {
    pub word: String,
    pub code: Option<String>,
    pub frequency: Option<u64>,
    pub source: String,
}
impl From<ImportedWord> for Word {
    fn from(value: ImportedWord) -> Self {
        Self {
            word: value.word,
            code: value.code,
            frequency: value.frequency,
            source: value.source,
        }
    }
}
impl From<Word> for ImportedWord {
    fn from(value: Word) -> Self {
        Self {
            word: value.word,
            code: value.code,
            frequency: value.frequency,
            source: value.source,
        }
    }
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct Summary {
    pub input_rows: usize,
    pub unique_rows: usize,
    pub duplicates: usize,
    pub exportable_rows: usize,
    pub missing_code_rows: usize,
}
#[derive(Clone, Debug, Serialize)]
pub struct Preview {
    pub format: Format,
    pub summary: Summary,
    pub sample: Vec<Word>,
    pub sample_truncated: bool,
    pub normalization: &'static str,
    pub warning: Option<&'static str>,
}
pub struct ImportResult {
    pub preview: Preview,
    words: Vec<Word>,
}

pub struct TxtImporter {
    pub source: String,
}
pub struct CsvImporter {
    pub source: String,
}
impl Importer for TxtImporter {
    fn import(&self, bytes: &[u8]) -> Result<Vec<ImportedWord>, String> {
        parse_txt(bytes, &self.source).map_err(|e| e.to_string())
    }
}
impl Importer for CsvImporter {
    fn import(&self, bytes: &[u8]) -> Result<Vec<ImportedWord>, String> {
        parse_csv(bytes, &self.source).map_err(|e| e.to_string())
    }
}
/// Stable (word, code) order; duplicate frequency is the maximum, not the sum.
/// Source is the lexicographically smallest nonempty source (order independent).
pub struct Normalize;
impl Converter for Normalize {
    fn convert(&self, words: Vec<ImportedWord>) -> Result<Vec<ImportedWord>, String> {
        if words.len() > MAX_ROWS {
            return Err("too many rows".into());
        }
        let mut unique: BTreeMap<(String, Option<String>), ImportedWord> = BTreeMap::new();
        let mut record_bytes = 0;
        for (index, word) in words.into_iter().enumerate() {
            // The converter is independently safe for future Importer implementations.
            let checked = validate_word(
                &word.word,
                word.code.as_deref(),
                word.frequency,
                &word.source,
                index + 1,
            )
            .map_err(|e| e.to_string())?;
            budget(&checked, &mut record_bytes, index + 1).map_err(|e| e.to_string())?;
            let key = (checked.word.clone(), checked.code.clone());
            if let Some(existing) = unique.get_mut(&key) {
                existing.frequency = existing.frequency.max(checked.frequency);
                if existing.source.is_empty()
                    || (!checked.source.is_empty() && checked.source < existing.source)
                {
                    existing.source = checked.source;
                }
            } else {
                unique.insert(key, checked);
            }
        }
        Ok(unique.into_values().collect())
    }
}

pub fn import(bytes: &[u8], format: Format, source: &str) -> Result<ImportResult, ImportError> {
    validate_source(source, None)?;
    // Validate shared bounds before dispatch; production uses the same Importer
    // contract as future source-level import plugins, followed by Converter.
    text(bytes)?;
    let provider: Box<dyn Importer> = match format {
        Format::Txt => Box::new(TxtImporter {
            source: source.into(),
        }),
        Format::Csv => Box::new(CsvImporter {
            source: source.into(),
        }),
    };
    let imported = provider.import(bytes).map_err(provider_error)?;
    let input_rows = imported.len();
    let words: Vec<Word> = Normalize
        .convert(imported)
        .map_err(|e| ImportError::new("invalid", None, e))?
        .into_iter()
        .map(Word::from)
        .collect();
    let missing_code_rows = words.iter().filter(|word| word.code.is_none()).count();
    let summary = Summary {
        input_rows,
        unique_rows: words.len(),
        duplicates: input_rows - words.len(),
        exportable_rows: words.len() - missing_code_rows,
        missing_code_rows,
    };
    let preview = Preview {
        format, summary, sample: words.iter().take(MAX_SAMPLE_ROWS).cloned().collect(), sample_truncated: words.len() > MAX_SAMPLE_ROWS,
        normalization: "trim outer word/code whitespace; collapse code spaces; duplicate (word,code): max frequency, lexicographically smallest nonempty source; stable word/code order",
        warning: (missing_code_rows > 0).then_some("Rows without code are kept in metadata and excluded from the dictionary. No pronunciation or code is inferred."),
    };
    Ok(ImportResult { preview, words })
}

fn provider_error(message: String) -> ImportError {
    if let Some((line, body)) = message
        .strip_prefix("line ")
        .and_then(|rest| rest.split_once(": "))
    {
        if let Ok(line) = line.parse() {
            return ImportError::new("invalid", Some(line), body);
        }
    }
    ImportError::new("invalid", None, message)
}

fn text(bytes: &[u8]) -> Result<&str, ImportError> {
    if bytes.len() > MAX_INPUT_BYTES {
        return Err(ImportError::new("limit", None, "input exceeds 16 MiB"));
    }
    let value = std::str::from_utf8(bytes).map_err(|_| {
        ImportError::new(
            "encoding",
            None,
            "input must be UTF-8 (optional UTF-8 BOM); other encodings require conversion",
        )
    })?;
    Ok(value.strip_prefix('\u{feff}').unwrap_or(value))
}
fn validate_source(source: &str, line: Option<usize>) -> Result<(), ImportError> {
    if source.len() > 4096
        || source
            .chars()
            .any(|value| value.is_control() && !matches!(value, '\n' | '\r' | '\t'))
    {
        return Err(ImportError::new(
            "invalid",
            line,
            "source exceeds 4096 bytes or contains unsupported control characters",
        ));
    }
    // CSV sources may contain quoted newlines; they only enter JSON metadata.
    Ok(())
}
fn validate_word(
    word: &str,
    code: Option<&str>,
    frequency: Option<u64>,
    source: &str,
    line: usize,
) -> Result<ImportedWord, ImportError> {
    if word.chars().any(char::is_control) {
        return Err(ImportError::new(
            "invalid",
            Some(line),
            "word contains a control character (tabs/newlines are not dictionary text)",
        ));
    }
    let word = word.trim();
    if word.is_empty() || word.len() > 1024 || word.starts_with('#') {
        return Err(ImportError::new(
            "invalid",
            Some(line),
            "word is empty, exceeds 1024 bytes or begins with Rime's comment marker #",
        ));
    }
    let code = if let Some(code) = code {
        if code.chars().any(char::is_control) {
            return Err(ImportError::new(
                "invalid",
                Some(line),
                "code contains a control character",
            ));
        }
        if code.len() > 1024 {
            return Err(ImportError::new(
                "invalid",
                Some(line),
                "code exceeds 1024 bytes",
            ));
        }
        let normalized = code
            .trim()
            .split(' ')
            .filter(|part| !part.is_empty())
            .collect::<Vec<_>>()
            .join(" ");
        if normalized.is_empty() {
            None
        } else {
            Some(normalized)
        }
    } else {
        None
    };
    validate_source(source, Some(line))?;
    Ok(ImportedWord {
        word: word.into(),
        code,
        frequency,
        source: source.into(),
    })
}
fn frequency(value: &str, line: usize) -> Result<Option<u64>, ImportError> {
    let value = value.trim();
    if value.is_empty() {
        return Ok(None);
    }
    if !value.bytes().all(|c| c.is_ascii_digit()) {
        return Err(ImportError::new(
            "invalid",
            Some(line),
            "frequency must be an unsigned integer",
        ));
    }
    value
        .parse()
        .map(Some)
        .map_err(|_| ImportError::new("invalid", Some(line), "frequency exceeds u64"))
}
fn budget(word: &ImportedWord, bytes: &mut usize, line: usize) -> Result<(), ImportError> {
    *bytes += word.word.len() + word.code.as_ref().map_or(0, String::len) + word.source.len() + 64;
    if *bytes > MAX_RECORD_BYTES {
        return Err(ImportError::new(
            "limit",
            Some(line),
            "expanded records exceed 32 MiB; use a shorter source or split the input",
        ));
    }
    Ok(())
}
fn push(
    words: &mut Vec<ImportedWord>,
    bytes: &mut usize,
    word: ImportedWord,
    line: usize,
) -> Result<(), ImportError> {
    if words.len() == MAX_ROWS {
        return Err(ImportError::new(
            "limit",
            Some(line),
            "input exceeds 100000 data rows",
        ));
    }
    budget(&word, bytes, line)?;
    words.push(word);
    Ok(())
}
fn parse_txt(bytes: &[u8], source: &str) -> Result<Vec<ImportedWord>, ImportError> {
    let text = text(bytes)?;
    let mut words = Vec::new();
    let mut record_bytes = 0;
    for (index, line) in text.split('\n').enumerate() {
        let line = line.strip_suffix('\r').unwrap_or(line);
        if line.trim().is_empty() || line.trim_start().starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = line.split('\t').collect();
        if fields.len() > 3 {
            return Err(ImportError::new("columns", Some(index + 1), "TXT rows require word, optional code and optional frequency separated by TAB (at most 3 columns)"));
        }
        let freq = fields
            .get(2)
            .map(|v| frequency(v, index + 1))
            .transpose()?
            .flatten();
        let word = validate_word(fields[0], fields.get(1).copied(), freq, source, index + 1)?;
        push(&mut words, &mut record_bytes, word, index + 1)?;
    }
    Ok(words)
}

// csv's fast parser intentionally accepts some malformed quotation sequences.
// Validate RFC-style quotation grammar first, then use csv for record parsing,
// escaped quotes, physical-line positions and UTF-8 fields. This is not split(',').
fn validate_csv_quoting(text: &str) -> Result<(), ImportError> {
    #[derive(Clone, Copy)]
    enum State {
        Start,
        Bare,
        Quoted,
        Closed,
    }
    let mut state = State::Start;
    let mut line = 1;
    let mut opened = 1;
    let mut bytes = text.bytes().peekable();
    while let Some(byte) = bytes.next() {
        match state {
            State::Start => match byte {
                b'"' => {
                    state = State::Quoted;
                    opened = line;
                }
                b',' => {}
                b'\n' => {
                    line += 1;
                }
                b'\r' => {
                    if bytes.peek() == Some(&b'\n') {
                        bytes.next();
                    }
                    line += 1;
                }
                _ => {
                    state = State::Bare;
                }
            },
            State::Bare => match byte {
                b'"' => {
                    return Err(ImportError::new(
                        "csv",
                        Some(line),
                        "a quote in an unquoted field is invalid",
                    ))
                }
                b',' => state = State::Start,
                b'\n' => {
                    line += 1;
                    state = State::Start;
                }
                b'\r' => {
                    if bytes.peek() == Some(&b'\n') {
                        bytes.next();
                    }
                    line += 1;
                    state = State::Start;
                }
                _ => {}
            },
            State::Quoted => match byte {
                b'"' => state = State::Closed,
                b'\n' => line += 1,
                b'\r' => {
                    if bytes.peek() == Some(&b'\n') {
                        bytes.next();
                    }
                    line += 1;
                }
                _ => {}
            },
            State::Closed => match byte {
                b'"' => state = State::Quoted,
                b',' => state = State::Start,
                b'\n' => {
                    line += 1;
                    state = State::Start;
                }
                b'\r' => {
                    if bytes.peek() == Some(&b'\n') {
                        bytes.next();
                    }
                    line += 1;
                    state = State::Start;
                }
                _ => {
                    return Err(ImportError::new(
                        "csv",
                        Some(line),
                        "only a delimiter or line end may follow a closing quote",
                    ))
                }
            },
        }
    }
    if matches!(state, State::Quoted) {
        return Err(ImportError::new(
            "csv",
            Some(opened),
            "unterminated quoted field",
        ));
    }
    Ok(())
}
fn parse_csv(bytes: &[u8], source: &str) -> Result<Vec<ImportedWord>, ImportError> {
    let text = text(bytes)?;
    validate_csv_quoting(text)?;
    let mut reader = csv::ReaderBuilder::new()
        .flexible(false)
        .from_reader(text.as_bytes());
    let headers = reader.headers().map_err(csv_error)?.clone();
    if headers.is_empty() {
        return Err(ImportError::new(
            "columns",
            Some(1),
            "CSV requires a header containing word",
        ));
    }
    let mut seen = BTreeSet::new();
    for header in &headers {
        if !matches!(header, "word" | "code" | "frequency" | "source") || !seen.insert(header) {
            return Err(ImportError::new("columns", Some(1), "CSV headers must be unique word, code, frequency, source; unknown headers are rejected"));
        }
    }
    let index = |key: &str| headers.iter().position(|header| header == key);
    let word_index = index("word")
        .ok_or_else(|| ImportError::new("columns", Some(1), "CSV requires the word header"))?;
    let code_index = index("code");
    let frequency_index = index("frequency");
    let source_index = index("source");
    let mut words = Vec::new();
    let mut record_bytes = 0;
    for row in reader.records() {
        let row = row.map_err(csv_error)?;
        let line = row
            .position()
            .map_or(1, |position| position.line() as usize);
        let freq = frequency_index
            .map(|index| frequency(&row[index], line))
            .transpose()?
            .flatten();
        let row_source = source_index.map_or(source, |index| {
            if row[index].is_empty() {
                source
            } else {
                &row[index]
            }
        });
        push(
            &mut words,
            &mut record_bytes,
            validate_word(
                &row[word_index],
                code_index.map(|index| &row[index]),
                freq,
                row_source,
                line,
            )?,
            line,
        )?;
    }
    Ok(words)
}
fn csv_error(error: csv::Error) -> ImportError {
    ImportError::new(
        "csv",
        error.position().map(|position| position.line() as usize),
        error.to_string(),
    )
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct PackageManifest {
    pub format_version: u32,
    pub kind: String,
    pub id: String,
    pub name: String,
    pub version: String,
    pub dictionary: String,
    pub metadata: String,
    pub schema_id: String,
    pub generated_dictionary: String,
    pub generated_schema: String,
    pub files: BTreeMap<String, PackageFile>,
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct PackageFile {
    pub sha256: String,
    pub bytes: u64,
}
#[derive(Debug, Serialize)]
pub struct ValidatedPackage {
    pub path: PathBuf,
    pub manifest: PackageManifest,
    pub summary: Summary,
}
#[derive(Debug, Serialize)]
pub struct WrittenPackage {
    pub path: PathBuf,
    pub manifest: PackageManifest,
    pub summary: Summary,
    pub warning: Option<String>,
}

/// Conservative IDs are also valid Rime dictionary and derived schema names.
pub fn valid_package_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && !matches!(id, "pinyin_simp" | "stroke" | "global")
        && !id.starts_with("myime_")
        && id.as_bytes()[0].is_ascii_lowercase()
        && id
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
}
fn metadata_field(value: &str, name: &str, max: usize) -> Result<(), ImportError> {
    if value.trim().is_empty() || value.len() > max || value.chars().any(char::is_control) {
        return Err(ImportError::new(
            "invalid",
            None,
            format!("{name} is empty, too long or contains a control character"),
        ));
    }
    Ok(())
}
/// Publish a new package. Existing files/directories are never overwritten.
/// manifest.json is the final commit marker: readers ignore incomplete packages.
pub fn write_package(
    packages_root: &Path,
    id: &str,
    name: &str,
    version: &str,
    imported: &ImportResult,
) -> Result<WrittenPackage, ImportError> {
    if !packages_root.is_absolute() {
        return Err(ImportError::new(
            "path",
            None,
            "packages_root must be absolute",
        ));
    }
    if !valid_package_id(id) {
        return Err(ImportError::new(
            "invalid",
            None,
            "id must start with a-z and contain only a-z, 0-9 or _ (maximum 64 bytes); pinyin_simp, stroke, global and myime_* are reserved",
        ));
    }
    metadata_field(name, "name", 256)?;
    metadata_field(version, "version", 64)?;
    if imported.preview.summary.exportable_rows == 0 {
        return Err(ImportError::new("no_codes", None, "there are no coded rows to export; supply codes from the target Rime dictionary's code system"));
    }
    fs::create_dir_all(packages_root).map_err(io_error)?;
    let root = fs::canonicalize(packages_root).map_err(io_error)?;
    let path = root.join(id);
    fs::create_dir(&path).map_err(|error| {
        ImportError::new(
            if error.kind() == std::io::ErrorKind::AlreadyExists {
                "exists"
            } else {
                "io"
            },
            None,
            format!("cannot create new package {id}: {error}"),
        )
    })?;
    let mut manifest = PackageManifest {
        format_version: 1,
        kind: "dictionary".into(),
        id: id.into(),
        name: name.into(),
        version: version.into(),
        dictionary: format!("{id}.dict.yaml"),
        metadata: "metadata.json".into(),
        schema_id: format!("myime_{id}"),
        generated_dictionary: format!("myime_{id}.dict.yaml"),
        generated_schema: format!("myime_{id}.schema.yaml"),
        files: BTreeMap::new(),
    };
    let mut created = Vec::new();
    let result = (|| {
        let dictionary = dictionary_text(&manifest, &imported.words)?;
        create_payload(
            &path,
            &manifest.dictionary,
            dictionary.as_bytes(),
            &mut created,
            &mut manifest.files,
        )?;
        // Product-owned Rime bridge data: one authoritative template in Rust.
        // The platform copies and deploys these files using the official engine.
        let aggregator = aggregator_text(&manifest)?;
        create_payload(
            &path,
            &manifest.generated_dictionary,
            aggregator.as_bytes(),
            &mut created,
            &mut manifest.files,
        )?;
        // An explicit root __patch disables Rime's automatic custom patch hook.
        // Include that hook deliberately, so future user YAML remains supported.
        let schema = schema_text(&manifest)?;
        create_payload(
            &path,
            &manifest.generated_schema,
            schema.as_bytes(),
            &mut created,
            &mut manifest.files,
        )?;
        let metadata = serde_json::json!({ "format_version": 1, "format": imported.preview.format, "summary": imported.preview.summary, "normalization": imported.preview.normalization, "words": imported.words });
        create_payload(
            &path,
            "metadata.json",
            &serde_json::to_vec_pretty(&metadata).map_err(json_error)?,
            &mut created,
            &mut manifest.files,
        )?;
        let guide = format!("# {name}\n\nThis is a data-only Rime dictionary package. It is not enabled automatically.\n\n- Imported dictionary: `{id}.dict.yaml` (name `{id}`).\n- MYIME-owned schema: `myime_{id}.schema.yaml`, ID `myime_{id}`.\n- Aggregation: `myime_{id}.dict.yaml` imports `pinyin_simp` and `{id}`.\n- Optional user patch hook: `myime_{id}.custom.yaml`.\n- Missing codes are reported in metadata.json and excluded from the dictionary.\n- No source word is converted to pinyin or another input code.\n- No existing Rime userdb/schema/custom patch has been modified.\n\nEnable explicitly through MYIME's isolated dictionary deployment workflow, then select `myime_{id}`; restore the original schema to roll back. This round supports the shipped `pinyin_simp` base; codes must use its full pinyin code system, not shuangpin keystrokes or another schema's codes. Rime rebuild/deploy happens outside the input hot path. The derived dictionary has its own Rime learning identity; existing base-schema learning is not forcibly copied/merged.\n\nRime format reference: https://github.com/rime/home/wiki/RimeWithSchemata\nRime include/custom-patch reference: https://github.com/rime/home/wiki/Configuration\n");
        create_payload(
            &path,
            "README.md",
            guide.as_bytes(),
            &mut created,
            &mut manifest.files,
        )?;
        create_file(
            &path,
            "manifest.json",
            &serde_json::to_vec_pretty(&manifest).map_err(json_error)?,
            &mut created,
        )?;
        Ok(WrittenPackage {
            path: path.clone(),
            manifest,
            summary: imported.preview.summary.clone(),
            warning: imported.preview.warning.map(str::to_string),
        })
    })();
    if result.is_err() {
        // Delete only files created by this call and the now-empty directory.
        // Never recursively remove an existing user package or unknown content.
        for filename in created.iter().rev() {
            let _ = fs::remove_file(path.join(filename));
        }
        let _ = fs::remove_dir(&path);
    }
    result
}
fn dictionary_text(manifest: &PackageManifest, words: &[Word]) -> Result<String, ImportError> {
    let mut text = format!("# Rime dictionary generated by MYIME; no userdb is modified.\n---\nname: {}\nversion: {}\nsort: by_weight\nuse_preset_vocabulary: false\ncolumns: [text, code, weight]\n...\n", manifest.id, serde_json::to_string(&manifest.version).map_err(json_error)?);
    for word in words {
        if let Some(code) = &word.code {
            text.push_str(&format!(
                "{}\t{}\t{}\n",
                word.word,
                code,
                word.frequency.unwrap_or(1)
            ));
        }
    }
    Ok(text)
}
fn aggregator_text(manifest: &PackageManifest) -> Result<String, ImportError> {
    Ok(format!("# MYIME dictionary aggregation; source dictionaries remain unchanged.\n---\nname: {}\nversion: {}\nsort: by_weight\nuse_preset_vocabulary: false\nimport_tables:\n  - pinyin_simp\n  - {}\n...\n", manifest.schema_id, serde_json::to_string(&manifest.version).map_err(json_error)?, manifest.id))
}
fn schema_text(manifest: &PackageManifest) -> Result<String, ImportError> {
    owned_schema_text(&manifest.schema_id, &manifest.name)
}
fn owned_schema_text(schema_id: &str, name: &str) -> Result<String, ImportError> {
    // SchemaUpdate first reads schema/schema_id from the RAW YAML, before
    // compiling includes/patches. Keep identity directly readable. The nested
    // include preserves the base schema metadata, including stroke dependency.
    Ok(format!("# MYIME-owned schema; retains the official base and optional user custom patch.\n__include: pinyin_simp.schema:/\nschema:\n  __include: pinyin_simp.schema:/schema\n  schema_id: {}\n  name: {}\n__patch:\n  - myime_dictionary_patch\n  - {}.custom:/patch?\nmyime_dictionary_patch:\n  translator/dictionary: {}\n", schema_id, serde_json::to_string(name).map_err(json_error)?, schema_id, schema_id))
}
#[derive(Debug, Serialize)]
pub struct CombinedDictionary {
    pub schema: String,
    pub packages: Vec<String>,
}
/// Build presentation-independent global aggregation in an unpublished workspace.
/// Source dictionaries and per-package schemas are left intact for AppProfiles.
/// The platform must deploy and verify this directory before publishing it.
pub fn combine_dictionaries(shared: &Path) -> Result<CombinedDictionary, ImportError> {
    if !shared.is_absolute() {
        return Err(ImportError::new(
            "path",
            None,
            "shared_dir must be absolute",
        ));
    }
    let directory = fs::symlink_metadata(shared).map_err(io_error)?;
    if !directory.is_dir() || directory.file_type().is_symlink() {
        return Err(ImportError::new(
            "path",
            None,
            "shared_dir must be a regular directory",
        ));
    }
    let shared = fs::canonicalize(shared).map_err(io_error)?;
    if shared.parent().is_some_and(|p| p.join("ready").exists()) {
        return Err(ImportError::new(
            "path",
            None,
            "Will not modify an already-published workspace",
        ));
    }
    let mut packages = BTreeSet::new();
    let mut entries = 0;
    for entry in fs::read_dir(&shared).map_err(io_error)? {
        let entry = entry.map_err(io_error)?;
        entries += 1;
        if entries > 10000 {
            return Err(ImportError::new(
                "limit",
                None,
                "Too many workspace entries",
            ));
        }
        let name = entry.file_name();
        let Some(name) = name.to_str() else {
            continue;
        };
        let Some(id) = name
            .strip_prefix("myime_")
            .and_then(|s| s.strip_suffix(".dict.yaml"))
        else {
            continue;
        };
        if id == "global" {
            continue;
        }
        if !valid_package_id(id) {
            return Err(ImportError::new(
                "invalid",
                None,
                format!("Invalid enabled package ID: {id}"),
            ));
        }
        for file in [
            format!("myime_{id}.dict.yaml"),
            format!("{id}.dict.yaml"),
            format!("myime_{id}.schema.yaml"),
        ] {
            let path = shared.join(&file);
            let metadata = fs::symlink_metadata(&path).map_err(io_error)?;
            if !metadata.is_file()
                || metadata.file_type().is_symlink()
                || fs::canonicalize(&path).map_err(io_error)?.parent() != Some(shared.as_path())
            {
                return Err(ImportError::new(
                    "path",
                    None,
                    format!("Enabled dictionary source must be a contained regular file: {file}"),
                ));
            }
            if metadata.len() > MAX_RECORD_BYTES as u64 {
                return Err(ImportError::new(
                    "limit",
                    None,
                    format!("Dictionary source exceeds 32 MiB: {file}"),
                ));
            }
        }
        packages.insert(id.to_owned());
        if packages.len() > 64 {
            return Err(ImportError::new(
                "limit",
                None,
                "At most 64 enabled packages are supported",
            ));
        }
    }
    let packages: Vec<String> = packages.into_iter().collect();
    if packages.is_empty() {
        return Ok(CombinedDictionary {
            schema: "pinyin_simp".into(),
            packages,
        });
    }
    let mut dictionary=String::from("# MYIME global aggregation; only explicitly deployed packages are included.\n---\nname: myime_global\nversion: '1'\nsort: by_weight\nuse_preset_vocabulary: false\nimport_tables:\n  - pinyin_simp\n");
    for id in &packages {
        dictionary.push_str(&format!("  - {id}\n"));
    }
    dictionary.push_str("...\n");
    let schema = owned_schema_text("myime_global", "MYIME 全局词库")?;
    for (name, text) in [
        ("myime_global.dict.yaml", dictionary),
        ("myime_global.schema.yaml", schema),
    ] {
        let path = shared.join(name);
        let snapshot = crate::config_bridge::read_snapshot(&path, 64 * 1024)
            .map_err(|e| ImportError::new(&e.kind, None, e.message))?;
        crate::config_bridge::save_text(&path, &snapshot.revision, &text, 64 * 1024)
            .map_err(|e| ImportError::new(&e.kind, None, e.message))?;
    }
    Ok(CombinedDictionary {
        schema: "myime_global".into(),
        packages,
    })
}
fn create_payload(
    path: &Path,
    filename: &str,
    bytes: &[u8],
    created: &mut Vec<String>,
    files: &mut BTreeMap<String, PackageFile>,
) -> Result<(), ImportError> {
    if bytes.len() > MAX_PACKAGE_FILE_BYTES {
        return Err(ImportError::new(
            "limit",
            None,
            "package payload exceeds 64 MiB",
        ));
    }
    create_file(path, filename, bytes, created)?;
    files.insert(
        filename.into(),
        PackageFile {
            sha256: crate::config_bridge::sha256(bytes),
            bytes: bytes.len() as u64,
        },
    );
    Ok(())
}
/// Validate an immutable import package before isolated Rime deployment.
/// Hashes detect edits, not publisher trust. Generated executable configuration
/// must also match MYIME's data templates; arbitrary schema directives are refused.
pub fn validate_package(package: &Path) -> Result<ValidatedPackage, ImportError> {
    if !package.is_absolute() {
        return Err(ImportError::new(
            "path",
            None,
            "package path must be absolute",
        ));
    }
    let metadata = fs::symlink_metadata(package).map_err(io_error)?;
    if !metadata.is_dir() || metadata.file_type().is_symlink() {
        return Err(ImportError::new(
            "path",
            None,
            "package must be a regular directory, not a symbolic link",
        ));
    }
    let root = fs::canonicalize(package).map_err(io_error)?;
    let bytes = read_regular(&root, "manifest.json", 64 * 1024)?;
    let manifest: PackageManifest = serde_json::from_slice(&bytes).map_err(json_error)?;
    if manifest.format_version != 1
        || manifest.kind != "dictionary"
        || !valid_package_id(&manifest.id)
        || root.file_name().and_then(|name| name.to_str()) != Some(manifest.id.as_str())
        || manifest.dictionary != format!("{}.dict.yaml", manifest.id)
        || manifest.metadata != "metadata.json"
        || manifest.schema_id != format!("myime_{}", manifest.id)
        || manifest.generated_dictionary != format!("{}.dict.yaml", manifest.schema_id)
        || manifest.generated_schema != format!("{}.schema.yaml", manifest.schema_id)
    {
        return Err(ImportError::new(
            "invalid",
            None,
            "manifest type, ID, directory name or generated file names are inconsistent",
        ));
    }
    metadata_field(&manifest.name, "name", 256)?;
    metadata_field(&manifest.version, "version", 64)?;
    let expected_files = [
        manifest.dictionary.as_str(),
        manifest.generated_dictionary.as_str(),
        manifest.generated_schema.as_str(),
        "metadata.json",
        "README.md",
    ];
    if manifest.files.len() != expected_files.len()
        || expected_files
            .iter()
            .any(|filename| !manifest.files.contains_key(*filename))
    {
        return Err(ImportError::new(
            "invalid",
            None,
            "manifest payload list must contain exactly the five known package files",
        ));
    }
    let mut payloads = BTreeMap::new();
    for filename in expected_files {
        let record = &manifest.files[filename];
        let limit = if filename == "metadata.json" {
            MAX_PACKAGE_FILE_BYTES
        } else if filename == manifest.dictionary {
            MAX_RECORD_BYTES
        } else {
            64 * 1024
        };
        if record.bytes > limit as u64
            || record.sha256.len() != 64
            || !record.sha256.bytes().all(|byte| byte.is_ascii_hexdigit())
        {
            return Err(ImportError::new(
                "invalid",
                None,
                "manifest contains an invalid file size or SHA-256",
            ));
        }
        let bytes = read_regular(&root, filename, limit)?;
        if bytes.len() as u64 != record.bytes
            || crate::config_bridge::sha256(&bytes) != record.sha256
        {
            return Err(ImportError::new(
                "changed",
                None,
                format!("package file changed or is incomplete: {filename}"),
            ));
        }
        payloads.insert(filename.to_string(), bytes);
    }
    #[derive(Deserialize)]
    struct Metadata {
        format_version: u32,
        format: Format,
        summary: Summary,
        words: Vec<Word>,
    }
    let data: Metadata = serde_json::from_slice(&payloads["metadata.json"]).map_err(json_error)?;
    let _ = data.format; // enum validation rejects unknown import formats.
    if data.format_version != 1 || data.words.len() > MAX_ROWS || data.summary.input_rows > MAX_ROWS
    {
        return Err(ImportError::new(
            "limit",
            None,
            "metadata version or row count is invalid",
        ));
    }
    let normalized: Vec<Word> = Normalize
        .convert(data.words.iter().cloned().map(ImportedWord::from).collect())
        .map_err(provider_error)?
        .into_iter()
        .map(Word::from)
        .collect();
    let missing = normalized.iter().filter(|word| word.code.is_none()).count();
    if normalized != data.words
        || data.summary.unique_rows != normalized.len()
        || data.summary.input_rows < normalized.len()
        || data.summary.duplicates != data.summary.input_rows - normalized.len()
        || data.summary.missing_code_rows != missing
        || data.summary.exportable_rows != normalized.len() - missing
        || data.summary.exportable_rows == 0
    {
        return Err(ImportError::new(
            "invalid",
            None,
            "metadata words and summary are inconsistent or contain no coded rows",
        ));
    }
    for (filename, expected) in [
        (
            &manifest.dictionary,
            dictionary_text(&manifest, &normalized)?,
        ),
        (&manifest.generated_dictionary, aggregator_text(&manifest)?),
        (&manifest.generated_schema, schema_text(&manifest)?),
    ] {
        if payloads[filename] != expected.as_bytes() {
            return Err(ImportError::new(
                "invalid",
                None,
                format!("package data does not match the generated MYIME format: {filename}"),
            ));
        }
    }
    Ok(ValidatedPackage {
        path: root,
        manifest,
        summary: data.summary,
    })
}
fn read_regular(root: &Path, filename: &str, limit: usize) -> Result<Vec<u8>, ImportError> {
    let path = root.join(filename);
    let before = fs::symlink_metadata(&path).map_err(io_error)?;
    if !before.is_file()
        || before.file_type().is_symlink()
        || fs::canonicalize(&path).map_err(io_error)?.parent() != Some(root)
    {
        return Err(ImportError::new(
            "path",
            None,
            "package payload must be a regular file inside its directory",
        ));
    }
    if before.len() > limit as u64 {
        return Err(ImportError::new(
            "limit",
            None,
            "package payload exceeds its size limit",
        ));
    }
    let mut file = fs::File::open(&path).map_err(io_error)?;
    let mut bytes = Vec::new();
    Read::by_ref(&mut file)
        .take(limit as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(io_error)?;
    let after = file.metadata().map_err(io_error)?;
    let current = fs::symlink_metadata(&path).map_err(io_error)?;
    if bytes.len() > limit
        || before.len() != after.len()
        || before.modified().map_err(io_error)? != after.modified().map_err(io_error)?
        || after.len() != current.len()
        || after.modified().map_err(io_error)? != current.modified().map_err(io_error)?
        || current.file_type().is_symlink()
        || fs::canonicalize(&path).map_err(io_error)?.parent() != Some(root)
    {
        return Err(ImportError::new(
            "changed",
            None,
            "package payload changed while being read",
        ));
    }
    Ok(bytes)
}
fn create_file(
    path: &Path,
    filename: &str,
    bytes: &[u8],
    created: &mut Vec<String>,
) -> Result<(), ImportError> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path.join(filename))
        .map_err(io_error)?;
    created.push(filename.into());
    file.write_all(bytes).map_err(io_error)?;
    file.sync_all().map_err(io_error)?;
    Ok(())
}
fn io_error(error: std::io::Error) -> ImportError {
    ImportError::new("io", None, error.to_string())
}
fn json_error(error: serde_json::Error) -> ImportError {
    ImportError::new("json", None, error.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        sync::atomic::{AtomicU64, Ordering},
        time::{SystemTime, UNIX_EPOCH},
    };
    fn txt(value: &str) -> Result<ImportResult, ImportError> {
        import(value.as_bytes(), Format::Txt, "fixture")
    }
    #[test]
    fn global_aggregation_is_sorted_complete_and_retains_the_custom_hook() {
        let fixture = Fixture::new();
        let shared = fixture.0.join("shared");
        fs::create_dir(&shared).unwrap();
        for id in ["zeta", "alpha"] {
            for name in [
                format!("{id}.dict.yaml"),
                format!("myime_{id}.dict.yaml"),
                format!("myime_{id}.schema.yaml"),
            ] {
                fs::write(shared.join(name), "fixture source\n").unwrap();
            }
        }
        let result = combine_dictionaries(&shared).unwrap();
        assert_eq!(result.schema, "myime_global");
        assert_eq!(result.packages, vec!["alpha", "zeta"]);
        let dict = fs::read_to_string(shared.join("myime_global.dict.yaml")).unwrap();
        assert_eq!(dict,"# MYIME global aggregation; only explicitly deployed packages are included.\n---\nname: myime_global\nversion: '1'\nsort: by_weight\nuse_preset_vocabulary: false\nimport_tables:\n  - pinyin_simp\n  - alpha\n  - zeta\n...\n");
        let schema = fs::read_to_string(shared.join("myime_global.schema.yaml")).unwrap();
        assert_eq!(schema,"# MYIME-owned schema; retains the official base and optional user custom patch.\n__include: pinyin_simp.schema:/\nschema:\n  __include: pinyin_simp.schema:/schema\n  schema_id: myime_global\n  name: \"MYIME 全局词库\"\n__patch:\n  - myime_dictionary_patch\n  - myime_global.custom:/patch?\nmyime_dictionary_patch:\n  translator/dictionary: myime_global\n");
        assert!(schema.contains("  - myime_global.custom:/patch?\n"));
        assert_eq!(
            fs::read_to_string(shared.join("alpha.dict.yaml")).unwrap(),
            "fixture source\n"
        );
        assert_eq!(
            combine_dictionaries(&shared).unwrap().packages,
            vec!["alpha", "zeta"]
        );
        for entry in fs::read_dir(&shared).unwrap() {
            fs::remove_file(entry.unwrap().path()).unwrap();
        }
        fs::remove_dir(shared).unwrap();
    }
    #[test]
    fn combine_rejects_missing_sources_and_published_workspaces_without_writing() {
        let fixture = Fixture::new();
        let shared = fixture.0.join("shared");
        fs::create_dir(&shared).unwrap();
        assert_eq!(combine_dictionaries(&shared).unwrap().schema, "pinyin_simp");
        assert!(!shared.join("myime_global.dict.yaml").exists());
        assert!(!valid_package_id("global"));
        assert!(!valid_package_id("evil-name"));
        fs::write(shared.join("myime_fixture.dict.yaml"), "fixture").unwrap();
        assert!(combine_dictionaries(&shared).is_err());
        assert!(!shared.join("myime_global.dict.yaml").exists());
        fs::write(fixture.0.join("ready"), "published").unwrap();
        assert_eq!(combine_dictionaries(&shared).unwrap_err().kind, "path");
        fs::remove_file(fixture.0.join("ready")).unwrap();
        fs::remove_file(shared.join("myime_fixture.dict.yaml")).unwrap();
        fs::remove_dir(shared).unwrap();
    }
    #[test]
    fn txt_bom_crlf_duplicate_frequency_and_source() {
        let parsed = txt("\u{feff}# comment\r\n你好\tni  hao\t3\r\n你好\t ni hao \t9\r\n无编码\r\n你好\tni hao\r\n").unwrap();
        assert_eq!(
            parsed.preview.summary,
            Summary {
                input_rows: 4,
                unique_rows: 2,
                duplicates: 2,
                exportable_rows: 1,
                missing_code_rows: 1
            }
        );
        let row = parsed.words.iter().find(|row| row.word == "你好").unwrap();
        assert_eq!(row.code.as_deref(), Some("ni hao"));
        assert_eq!(row.frequency, Some(9));
        let rows = vec![
            ImportedWord {
                word: "你".into(),
                code: Some("ni".into()),
                frequency: None,
                source: "z".into(),
            },
            ImportedWord {
                word: "你".into(),
                code: Some("ni".into()),
                frequency: Some(2),
                source: "a".into(),
            },
        ];
        let converted = Normalize.convert(rows).unwrap();
        assert_eq!(converted[0].source, "a");
        assert_eq!(converted[0].frequency, Some(2));
    }
    #[test]
    fn csv_quotes_commas_multiline_source_and_header_order() {
        let parsed = import(
            "\u{feff}source,frequency,word,code\r\n\"a,b\r\nquote\"\"test\",8,\"逗号,词\",ci\r\n"
                .as_bytes(),
            Format::Csv,
            "fallback",
        )
        .unwrap();
        assert_eq!(parsed.words[0].word, "逗号,词");
        assert_eq!(parsed.words[0].source, "a,b\r\nquote\"test");
        assert_eq!(parsed.words[0].frequency, Some(8));
    }
    #[test]
    fn csv_malformed_quote_columns_and_physical_line_errors() {
        for (data, line) in [
            ("word,code\na\"b,x", 2),
            ("word,code\n\"ab\"x,x", 2),
            ("word,code\n\"ab,x", 2),
            ("word,code\na,x,y", 2),
            ("word,code\n\"a\nb\",x", 2),
        ] {
            let error = import(data.as_bytes(), Format::Csv, "s").err().unwrap();
            assert_eq!(error.line, Some(line), "{data}: {error}");
        }
        for data in ["word,word\na,b", "word,unknown\na,b", "code,frequency\na,1"] {
            assert!(import(data.as_bytes(), Format::Csv, "s").is_err());
        }
    }
    #[test]
    fn invalid_encoding_limits_frequency_and_injection() {
        assert_eq!(
            import(&[0xff], Format::Txt, "s").err().unwrap().kind,
            "encoding"
        );
        assert!(import(&vec![b'a'; MAX_INPUT_BYTES + 1], Format::Txt, "s").is_err());
        for value in [
            "词\tci\t-1",
            "词\tci\t18446744073709551616",
            "词\tci\t1\textra",
            "词\tx\0",
            "词\tci\t50%",
        ] {
            assert!(txt(value).is_err(), "{value}");
        }
        assert!(import(b"word,code\n#comment,x", Format::Csv, "s").is_err());
        assert!(txt(&"词\tx\n".repeat(MAX_ROWS + 1)).is_err());
    }
    struct Fixture(PathBuf);
    impl Fixture {
        fn new() -> Self {
            static SERIAL: AtomicU64 = AtomicU64::new(0);
            let stamp = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let path = std::env::temp_dir().join(format!(
                "myime-import-test-{}-{stamp}-{}",
                std::process::id(),
                SERIAL.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir(&path).unwrap();
            Self(path)
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            for id in ["fixture", "empty"] {
                let dir = self.0.join(id);
                for file in [
                    format!("{id}.dict.yaml"),
                    format!("myime_{id}.dict.yaml"),
                    format!("myime_{id}.schema.yaml"),
                    "metadata.json".into(),
                    "README.md".into(),
                    "manifest.json".into(),
                ] {
                    let _ = fs::remove_file(dir.join(file));
                }
                let _ = fs::remove_dir(dir);
            }
            let _ = fs::remove_dir(&self.0);
        }
    }
    #[test]
    fn package_is_data_only_excludes_missing_codes_and_never_overwrites() {
        let fixture = Fixture::new();
        let parsed = txt("你好\tni hao\t5\n无编码\n").unwrap();
        let written = write_package(&fixture.0, "fixture", "测试词库", "1.0.0", &parsed).unwrap();
        let dictionary = fs::read_to_string(written.path.join("fixture.dict.yaml")).unwrap();
        assert!(dictionary.contains("columns: [text, code, weight]\n...\n你好\tni hao\t5\n"));
        assert!(!dictionary.contains("无编码"));
        assert_eq!(written.manifest.schema_id, "myime_fixture");
        let aggregator =
            fs::read_to_string(written.path.join(&written.manifest.generated_dictionary)).unwrap();
        assert!(aggregator.contains("import_tables:\n  - pinyin_simp\n  - fixture\n"));
        let schema =
            fs::read_to_string(written.path.join(&written.manifest.generated_schema)).unwrap();
        assert!(schema.contains("__include: pinyin_simp.schema:/\n"));
        assert!(schema.contains(
            "schema:\n  __include: pinyin_simp.schema:/schema\n  schema_id: myime_fixture\n"
        ));
        assert!(schema.contains("myime_fixture.custom:/patch?\n"));
        assert_eq!(
            validate_package(&written.path).unwrap().manifest,
            written.manifest
        );
        let metadata = fs::read_to_string(written.path.join("metadata.json")).unwrap();
        assert!(metadata.contains("无编码"));
        assert_eq!(
            write_package(&fixture.0, "fixture", "again", "2.0", &parsed)
                .err()
                .unwrap()
                .kind,
            "exists"
        );
        assert_eq!(
            fs::read_to_string(written.path.join("fixture.dict.yaml")).unwrap(),
            dictionary
        );
        assert_eq!(
            write_package(&fixture.0, "empty", "empty", "1", &txt("无编码").unwrap())
                .err()
                .unwrap()
                .kind,
            "no_codes"
        );
        assert!(!fixture.0.join("empty").exists());
        for id in [
            "../x",
            "a/b",
            "-bad",
            "Ab",
            "x-y",
            "x\nname:",
            "",
            "pinyin_simp",
            "stroke",
            "myime_fixture",
        ] {
            assert!(!valid_package_id(id));
        }
    }
    #[test]
    fn package_validation_rejects_edits_paths_and_arbitrary_schema_directives() {
        let fixture = Fixture::new();
        let written = write_package(
            &fixture.0,
            "fixture",
            "验证词库",
            "1",
            &txt("词\tci\t5").unwrap(),
        )
        .unwrap();
        let path = &written.path;
        let manifest_path = path.join("manifest.json");
        let original_manifest = fs::read(&manifest_path).unwrap();
        let mut manifest = written.manifest.clone();
        manifest.dictionary = "../outside.dict.yaml".into();
        fs::write(&manifest_path, serde_json::to_vec(&manifest).unwrap()).unwrap();
        assert_eq!(validate_package(path).unwrap_err().kind, "invalid");
        fs::write(&manifest_path, &original_manifest).unwrap();
        let dictionary_path = path.join(&written.manifest.dictionary);
        let dictionary = fs::read(&dictionary_path).unwrap();
        fs::write(&dictionary_path, b"changed").unwrap();
        assert_eq!(validate_package(path).unwrap_err().kind, "changed");
        fs::write(&dictionary_path, &dictionary).unwrap();
        fs::remove_file(&dictionary_path).unwrap();
        fs::create_dir(&dictionary_path).unwrap();
        assert_eq!(validate_package(path).unwrap_err().kind, "path");
        fs::remove_dir(&dictionary_path).unwrap();
        fs::write(&dictionary_path, &dictionary).unwrap();
        // Recomputing a hash is not enough to introduce arbitrary Rime directives.
        let schema_path = path.join(&written.manifest.generated_schema);
        let schema = fs::read(&schema_path).unwrap();
        let altered = [
            schema.as_slice(),
            b"\nengine:\n  processors: [lua_processor@evil]\n",
        ]
        .concat();
        fs::write(&schema_path, &altered).unwrap();
        let mut manifest = written.manifest.clone();
        manifest.files.insert(
            manifest.generated_schema.clone(),
            PackageFile {
                sha256: crate::config_bridge::sha256(&altered),
                bytes: altered.len() as u64,
            },
        );
        fs::write(&manifest_path, serde_json::to_vec(&manifest).unwrap()).unwrap();
        assert_eq!(validate_package(path).unwrap_err().kind, "invalid");
        fs::write(schema_path, schema).unwrap();
        fs::write(&manifest_path, &original_manifest).unwrap();
        assert!(validate_package(path).is_ok());
        // A former template had identity only in __patch. SchemaUpdate reads
        // schema/schema_id BEFORE include/patch compilation, so reject it even
        // when someone recomputes all hashes to make the manifest consistent.
        let legacy=format!("__include: pinyin_simp.schema:/\n__patch:\n  schema/schema_id: {}\n  translator/dictionary: {}\n",written.manifest.schema_id,written.manifest.schema_id);
        let legacy_path = path.join(&written.manifest.generated_schema);
        let current = fs::read(&legacy_path).unwrap();
        fs::write(&legacy_path, legacy.as_bytes()).unwrap();
        let mut legacy_manifest = written.manifest.clone();
        legacy_manifest.files.insert(
            legacy_manifest.generated_schema.clone(),
            PackageFile {
                sha256: crate::config_bridge::sha256(legacy.as_bytes()),
                bytes: legacy.len() as u64,
            },
        );
        fs::write(
            &manifest_path,
            serde_json::to_vec(&legacy_manifest).unwrap(),
        )
        .unwrap();
        assert_eq!(validate_package(path).unwrap_err().kind, "invalid");
        fs::write(legacy_path, current).unwrap();
        fs::write(manifest_path, original_manifest).unwrap();
    }
}
