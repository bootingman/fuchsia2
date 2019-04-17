//! Expectations for Bluetooth Peers (i.e. Remote Devices)

use fidl_fuchsia_bluetooth_control::{RemoteDevice, TechnologyType};

use crate::expectation::Predicate;

pub fn name(expected_name: &str) -> Predicate<RemoteDevice> {
    Predicate::equal(Some(expected_name.to_string()))
        .over(|peer: &RemoteDevice| peer.name.clone(), ".name")
}
pub fn address(expected_address: &str) -> Predicate<RemoteDevice> {
    let address = expected_address.to_string();
    Predicate::<RemoteDevice>::new(
        move |peer| peer.address == address,
        Some(&format!("address == {}", expected_address)),
    )
}
pub fn technology(tech: TechnologyType) -> Predicate<RemoteDevice> {
    Predicate::<RemoteDevice>::new(
        move |peer| peer.technology == tech,
        Some(&format!("technology == {:?}", tech)),
    )
}
pub fn connected(connected: bool) -> Predicate<RemoteDevice> {
    Predicate::<RemoteDevice>::new(
        move |peer| peer.connected == connected,
        Some(&format!("connected == {}", connected)),
    )
}
pub fn bonded(bonded: bool) -> Predicate<RemoteDevice> {
    Predicate::<RemoteDevice>::new(
        move |peer| peer.bonded == bonded,
        Some(&format!("bonded == {}", bonded)),
    )
}
