//! The `config` table — the resolved server config as scripts see it: the
//! historical `sipiConfGlobals` field inventory minus the dropped
//! `password`/`adminuser` credentials (ADR-0023 divergence: secrets are not
//! injected into request VMs). Plain values, so no function registers through
//! the chokepoint.

use mlua::Table;

use crate::runtime::RequestVm;

use super::ConfigValues;

pub fn install(vm: &RequestVm, cfg: &ConfigValues) -> mlua::Result<()> {
    let lua = vm.lua();
    let config: Table = lua.create_table()?;
    config.set("hostname", cfg.hostname.as_str())?;
    config.set("port", cfg.port)?;
    config.set("sslport", cfg.sslport)?;
    config.set("imgroot", cfg.imgroot.as_str())?;
    config.set("max_temp_file_age", cfg.max_temp_file_age)?;
    config.set("prefix_as_path", cfg.prefix_as_path)?;
    config.set("init_script", cfg.init_script.as_str())?;
    config.set("cache_dir", cfg.cache_dir.as_str())?;
    config.set("cache_size", cfg.cache_size)?;
    config.set("jpeg_quality", cfg.jpeg_quality)?;
    config.set("thumb_size", cfg.thumb_size.as_str())?;
    config.set("cache_n_files", cfg.cache_n_files)?;
    config.set("max_post_size", cfg.max_post_size)?;
    config.set("tmpdir", cfg.tmpdir.as_str())?;
    config.set("scriptdir", cfg.scriptdir.as_str())?;
    config.set("knora_path", cfg.knora_path.as_str())?;
    config.set("knora_port", cfg.knora_port.as_str())?;
    config.set("docroot", cfg.docroot.as_str())?;
    lua.globals().set("config", config)
}
