// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error, Fail, ResultExt},
    fuchsia_async::TimeoutExt,
    fuchsia_bluetooth::{error::Error as BTError, expectation::asynchronous::ExpectableStateExt},
    fuchsia_zircon::{Duration, DurationNum},
    futures::TryFutureExt,
};

use crate::harness::low_energy_central::CentralHarness;

mod central_expectation {
    use crate::harness::low_energy_central::{CentralState, ScanStateChange};
    use fuchsia_bluetooth::expectation::Predicate;
    use fuchsia_bluetooth::le::RemoteDevice;

    pub fn scan_enabled() -> Predicate<CentralState> {
        Predicate::equal(Some(ScanStateChange::ScanEnabled))
          .over_value(|state: &CentralState| state.scan_state_changes.last().cloned(), ".scan_state_changes.last()")
    }
    pub fn scan_disabled() -> Predicate<CentralState> {
        Predicate::equal(Some(ScanStateChange::ScanDisabled))
          .over_value(|state: &CentralState| state.scan_state_changes.last().cloned(), ".scan_state_changes.last()")
    }
    pub fn device_found(expected_name: &str) -> Predicate<CentralState> {
        let expected_name = expected_name.to_string();
        //let msg = format!("Peer '{}' has been discovered", expected_name);
        /*
        // TODO(nickpollard) - use 'any' predicate
        // TODO - test with an incorrect value to see what the error is like
        let has_expected_name = move |peer: &RemoteDevice| -> bool {
            peer.advertising_data
                .as_ref()
                .and_then(|ad| ad.name.as_ref())
                .iter()
                .any(|&name| name == &expected_name)
        };

        Predicate::new(
            move |state: &CentralState| -> bool {
                !state.remote_devices.is_empty()
                    && state.remote_devices.iter().any(&has_expected_name)
            },
            msg,
        )
        */

        let has_expected_name = Predicate::equal(Some(expected_name))
            .over_value(|peer: &RemoteDevice| {
                peer.advertising_data
                    .as_ref()
                    .and_then(|ad| ad.name.as_ref().cloned())
            },
            ".advertising_data.name"
            );

        Predicate::any(has_expected_name).over(|state: &CentralState| { &state.remote_devices }, ".remote_devices")

    }
}

fn scan_timeout() -> Duration {
    10.seconds()
}

async fn start_scan(central: &CentralHarness) -> Result<(), Error> {
    let status = await!(central
        .aux()
        .start_scan(None)
        .map_err(|e| e.context("FIDL error sending command").into())
        .on_timeout(scan_timeout().after_now(), move || Err(err_msg("Timed out"))))
    .context("Could not initialize scan")?;
    if let Some(e) = status.error {
        return Err(BTError::from(*e).into());
    }
    Ok(())
}

pub async fn enable_scan(central: CentralHarness) -> Result<(), Error> {
    await!(start_scan(&central))?;
    let _ = await!(central.when_satisfied(
        central_expectation::scan_enabled().and(central_expectation::device_found("Fake")),
        scan_timeout()
    ))?;
    Ok(())
}

pub async fn enable_and_disable_scan(central: CentralHarness) -> Result<(), Error> {
    await!(start_scan(&central))?;
    let _ = await!(central.when_satisfied(central_expectation::scan_enabled(), scan_timeout()))?;
    let _ = central.aux().stop_scan()?;
    let _ = await!(central.when_satisfied(central_expectation::scan_disabled(), scan_timeout()))?;
    Ok(())
}
