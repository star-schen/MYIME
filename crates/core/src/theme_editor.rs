//! Offline editor metadata and comment-preserving draft updates.
//! Runtime validation remains Theme::parse; GUI never saves unvalidated drafts.
use crate::{config_bridge::{BridgeError, MAX_CONFIG_BYTES}, theme::{Theme, MAX_MANIFEST_BYTES}};
use serde_json::{json, Map, Value as Json};
use toml_edit::{DocumentMut, Item, Table, Value};

pub fn describe(text: &str) -> Result<Json, BridgeError> {
    let theme=Theme::parse(text).map_err(|e| BridgeError::new("invalid",e))?;
    let mut values=include_str!("../../../themes/default/theme.toml").parse::<toml::Table>()
        .map_err(|e| BridgeError::new("invalid",e.to_string()))?;
    let custom=text.parse::<toml::Table>().map_err(|e| BridgeError::new("invalid",e.to_string()))?;
    crate::config::merge(&mut values,&custom);
    let mut fields=Vec::new();
    for section in ["font","layout","shadow","display"] {
        if let Some(table)=values.get(section).and_then(toml::Value::as_table) {
            for (key,value) in table {
                fields.push(json!({"section":section,"category":format!("theme.category.{section}"),"key":key,"value":value,
                    "kind":if value.is_bool(){"boolean"}else if value.is_integer(){"integer"}else{"string"},
                    "choices":if key=="orientation"{json!(["vertical","horizontal"])}else{Json::Null}}));
            }
        }
    }
    let mut color = |section: &str, key: &str, value: u32| fields.push(json!({"section":section,
        "category":format!("theme.category.{section}"),"key":key,"value":format!("#{value:06X}"),"kind":"color"}));
    color("background", "color", theme.background);
    color("input.background", "color", theme.input.background.color);
    color("input.background", "border", theme.input.background.border);
    color("input.characters", "color", theme.input.text);
    color("input.characters", "caret", theme.input.caret);
    color("candidates.background", "color", theme.candidates.background.color);
    color("candidates.background", "border", theme.candidates.background.border);
    color("candidates.background", "highlight", theme.candidates.highlight);
    color("candidates.characters", "color", theme.candidates.text);
    color("candidates.characters", "comment", theme.candidates.muted);
    color("candidates.characters", "label", theme.candidates.label);
    color("candidates.characters", "selected", theme.candidates.highlight_text);
    for (section, background) in [("input.background", &theme.input.background), ("candidates.background", &theme.candidates.background)] {
        for (key,value) in [("border_width",background.border_width),("border_radius",background.border_radius)] {
            fields.push(json!({"section":section,"category":format!("theme.category.{section}"),"key":key,"value":value,"kind":"integer"}));
        }
    }
    Ok(json!({"id":theme.id,"name":theme.name,"version":theme.version,"description":theme.description,"fields":fields}))
}
pub fn edit(text: &str,changes: &Map<String,Json>) -> Result<String,BridgeError> {
    if text.len()>MAX_MANIFEST_BYTES as usize || text.len()>MAX_CONFIG_BYTES {
        return Err(BridgeError::new("too_large","Theme exceeds 64 KiB"));
    }
    let mut document=text.parse::<DocumentMut>().map_err(|e| BridgeError::new("invalid",e.to_string()))?;
    for (path,value) in changes {
        let item=match value {
            Json::Bool(v)=>Item::Value(Value::from(*v)),
            Json::String(v)=>Item::Value(Value::from(v.as_str())),
            Json::Number(v)=>Item::Value(Value::from(v.as_i64().ok_or_else(||BridgeError::new("invalid","Integer required"))?)),
            _=>return Err(BridgeError::new("invalid","Unsupported theme field value")),
        };
        let parts:Vec<_>=path.split('.').collect();
        let (parent,key)=match parts.as_slice() {
            [key] if ["id","name","version","description"].contains(key)=>(document.as_table_mut(),*key),
            [section,key] if ["font","layout","colors","shadow","display"].contains(section)=>{
                if document.get(*section).is_none() { document.insert(*section,Item::Table(Table::new())); }
                let table=document.get_mut(*section).and_then(Item::as_table_mut)
                    .ok_or_else(||BridgeError::new("invalid","Theme section must be a table"))?;
                (table,*key)
            },
            [section,key] if *section=="background" && *key=="color"=>{
                if document.get(*section).is_none() { document.insert(*section,Item::Table(Table::new())); }
                (document.get_mut(*section).and_then(Item::as_table_mut).ok_or_else(||BridgeError::new("invalid","Background must be a table"))?,*key)
            },
            [card,section,key] if ["input","candidates"].contains(card) && ["background","characters"].contains(section)=>{
                if document.get(*card).is_none() { document.insert(*card,Item::Table(Table::new())); }
                let card_table=document.get_mut(*card).and_then(Item::as_table_mut).ok_or_else(||BridgeError::new("invalid","Card must be a table"))?;
                if card_table.get(*section).is_none() { card_table.insert(*section,Item::Table(Table::new())); }
                (card_table.get_mut(*section).and_then(Item::as_table_mut).ok_or_else(||BridgeError::new("invalid","Layer must be a table"))?,*key)
            },
            _=>return Err(BridgeError::new("invalid",format!("Unknown theme field: {path}"))),
        };
        let mut item=item;
        if let (Some(old),Some(new))=(parent.get(key).and_then(Item::as_value),item.as_value_mut()) {
            *new.decor_mut()=old.decor().clone();
        }
        parent.insert(key,item);
    }
    let result=document.to_string();
    Theme::parse(&result).map_err(|e|BridgeError::new("invalid",e))?;
    Ok(result)
}
