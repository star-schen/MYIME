//! Product config only: native Rime YAML is never rewritten here.
use std::{collections::BTreeMap, path::Path};
use toml::{Table, Value};
#[derive(Clone, Debug, PartialEq)]
pub struct EffectiveConfig {
    pub schema: String,
    pub enabled: bool,
    pub options: BTreeMap<String, bool>,
    pub values: Table,
}
#[derive(Default)]
pub struct Config {
    document: Table,
}
fn merge(base: &mut Table, layer: &Table) {
    for (key, value) in layer {
        match (base.get_mut(key), value) {
            (Some(Value::Table(old)), Value::Table(new)) => merge(old, new),
            _ => {
                base.insert(key.clone(), value.clone());
            }
        }
    }
}
impl Config {
    pub fn parse(text: &str) -> Result<Self, String> {
        let config = Self {
            document: text.parse::<Table>().map_err(|e| e.to_string())?,
        };
        config.effective("", &Table::new())?;
        if let Some(profiles) = config.document.get("profiles") {
            let profiles = profiles.as_array().ok_or("profiles must be an array")?;
            let mut seen = Vec::new();
            for profile in profiles {
                let exe = profile
                    .get("executable")
                    .and_then(Value::as_str)
                    .ok_or("Profile needs executable")?;
                if exe.is_empty() || exe.contains(['/', '\\']) {
                    return Err("Executable must be a basename".into());
                }
                if seen.iter().any(|s: &String| s.eq_ignore_ascii_case(exe)) {
                    return Err("Duplicate executable profile".into());
                }
                seen.push(exe.to_owned());
                config.effective(exe, &Table::new())?;
            }
        }
        Ok(config)
    }
    pub fn read(path: &Path) -> Result<Self, String> {
        match std::fs::read_to_string(path) {
            Ok(s) => Self::parse(&s),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(Self::default()),
            Err(e) => Err(e.to_string()),
        }
    }
    pub fn effective(&self, exe: &str, runtime: &Table) -> Result<EffectiveConfig, String> {
        let mut values: Table = "schema='pinyin_simp'\nenabled=true\n[options]\nascii_mode=false"
            .parse()
            .unwrap();
        if let Some(layer) = self.document.get("default") {
            merge(
                &mut values,
                layer.as_table().ok_or("default must be a table")?,
            );
        }
        if let Some(platforms) = self.document.get("platform") {
            if let Some(layer) = platforms
                .as_table()
                .ok_or("platform must be a table")?
                .get("windows")
            {
                merge(
                    &mut values,
                    layer.as_table().ok_or("platform.windows must be a table")?,
                );
            }
        }
        if let Some(profiles) = self.document.get("profiles") {
            for p in profiles.as_array().ok_or("profiles must be an array")? {
                if p.get("executable")
                    .and_then(Value::as_str)
                    .is_some_and(|s| s.eq_ignore_ascii_case(exe))
                {
                    if let Some(layer) = p.get("overrides") {
                        merge(
                            &mut values,
                            layer.as_table().ok_or("overrides must be a table")?,
                        );
                    }
                }
            }
        }
        merge(&mut values, runtime);
        let schema = values
            .get("schema")
            .and_then(Value::as_str)
            .ok_or("schema must be a string")?
            .to_owned();
        if schema.is_empty() {
            return Err("Empty schema".into());
        }
        let enabled = values
            .get("enabled")
            .and_then(Value::as_bool)
            .ok_or("enabled must be boolean")?;
        let options = values
            .get("options")
            .and_then(Value::as_table)
            .ok_or("options must be a table")?
            .iter()
            .map(|(k, v)| {
                v.as_bool()
                    .map(|b| (k.clone(), b))
                    .ok_or_else(|| format!("Option {k} must be boolean"))
            })
            .collect::<Result<_, _>>()?;
        Ok(EffectiveConfig {
            schema,
            enabled,
            options,
            values,
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn layers_inherit_without_inferred_dictionaries() {
        let c=Config::parse("[default]\nschema='base'\ndictionaries=['global']\n[default.options]\nfull_shape=true\n[[profiles]]\nexecutable='Game.exe'\n[profiles.overrides.options]\nascii_mode=true").unwrap();
        let e = c.effective("GAME.EXE", &Table::new()).unwrap();
        assert!(e.options["ascii_mode"] && e.options["full_shape"]);
        assert_eq!(e.schema, "base");
        assert_eq!(e.values["dictionaries"][0].as_str(), Some("global"));
        assert!(!c.effective("Editor.exe", &Table::new()).unwrap().options["ascii_mode"]);
    }
    #[test]
    fn runtime_wins_and_unknown_values_survive() {
        let c = Config::parse("[default]\nenabled=false\n[default.future]\nvalue=3").unwrap();
        let e = c
            .effective("x.exe", &"enabled=true".parse::<Table>().unwrap())
            .unwrap();
        assert!(e.enabled);
        assert_eq!(e.values["future"]["value"].as_integer(), Some(3));
    }
    #[test]
    fn invalid_and_duplicate_profiles_are_rejected() {
        assert!(Config::parse("[default]\nenabled='yes'").is_err());
        assert!(Config::parse(
            "[[profiles]]\nexecutable='x.exe'\n[[profiles]]\nexecutable='X.exe'"
        )
        .is_err());
    }
}
