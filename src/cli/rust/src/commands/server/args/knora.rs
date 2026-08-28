//! Knora integration flags (the "Knora" `--help` heading).

use clap::Args;

#[derive(Args, Debug)]
#[command(next_help_heading = "Knora")]
pub struct KnoraArgs {
    /// Knora server host.
    #[arg(long, env = "SIPI_KNORAPATH", value_name = "HOST")]
    pub knorapath: Option<String>,
    /// Knora server port (a string in the engine config).
    #[arg(long, env = "SIPI_KNORAPORT", value_name = "PORT")]
    pub knoraport: Option<String>,
}
