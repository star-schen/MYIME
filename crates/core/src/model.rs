use std::collections::BTreeMap;
#[derive(Default, Debug)]
pub struct Candidate {
    pub text: String,
    pub comment: String,
    pub label: String,
    pub index: usize,
}
#[derive(Default, Debug)]
pub struct InputState {
    pub preedit: String,
    /// UTF-8 byte offset, converted to UTF-16 only by the Windows host.
    pub caret: usize,
    pub candidates: Vec<Candidate>,
    pub selected: usize,
    pub page: usize,
    pub page_size: usize,
    pub last_page: bool,
    /// Retained until the host acknowledges a successful document transaction.
    pub commit: String,
    pub schema: String,
    pub options: BTreeMap<String, bool>,
    pub active: bool,
}
