//use crate::focus;
use crate::expectation::*;
use fidl_fuchsia_bluetooth_control::{Appearance, RemoteDevice, TechnologyType};

const TEST_PEER_NAME: &'static str = "TestPeer";
const TEST_PEER_ADDRESS: &'static str = "00:00:00:00:00:01";
const INCORRECT_PEER_NAME: &'static str = "IncorrectPeer";
const INCORRECT_PEER_ADDRESS: &'static str = "00:00:00:00:00:02";

fn correct_name() -> Predicate<RemoteDevice> {
    peer::name(TEST_PEER_NAME)
}
fn incorrect_name() -> Predicate<RemoteDevice> {
    peer::name(INCORRECT_PEER_NAME)
}
fn correct_address() -> Predicate<RemoteDevice> {
    peer::address(TEST_PEER_ADDRESS)
}
fn incorrect_address() -> Predicate<RemoteDevice> {
    peer::address(INCORRECT_PEER_ADDRESS)
}

fn test_peer() -> RemoteDevice {
    RemoteDevice {
        name: Some(TEST_PEER_NAME.into()),
        address: TEST_PEER_ADDRESS.into(),
        technology: TechnologyType::LowEnergy,
        connected: false,
        bonded: false,
        appearance: Appearance::Unknown,
        identifier: "".into(),
        rssi: None,
        tx_power: None,
        service_uuids: vec![],
    }
}


#[test]
fn test() -> Result<(),AssertionText> {
    correct_name().satisfied_(&test_peer())
}









#[test]
fn simple_predicate_succeeds() {
    let predicate = Predicate::<RemoteDevice>::new(
        move |peer| peer.name == Some(TEST_PEER_NAME.into()),
        None,
        );
    assert!(predicate.satisfied(&test_peer()));
}
#[test]
fn simple_incorrect_predicate_fail() {
    let predicate = Predicate::<RemoteDevice>::new(
        move |peer| peer.name == Some("INCORRECT_NAME".into()),
        None,
        );
    assert!(!predicate.satisfied(&test_peer()));
}

#[test]
fn predicate_and_both_true_succeeds() {
    let predicate = correct_name().and(correct_address());
    assert!(predicate.satisfied(&test_peer()));
}

#[test]
fn predicate_and_one_or_more_false_fails() {
    let predicate = correct_name().and(incorrect_address());
    assert!(!predicate.satisfied(&test_peer()));

    let predicate = incorrect_name().and(correct_address());
    assert!(!predicate.satisfied(&test_peer()));

    let predicate = incorrect_name().and(incorrect_address());
    assert!(!predicate.satisfied(&test_peer()));
}

#[test]
fn predicate_or_both_false_fails() {
    let predicate = incorrect_name().or(incorrect_address());
    assert!(!predicate.satisfied(&test_peer()));
}

#[test]
fn predicate_or_one_or_more_true_succeeds() {
    let predicate = correct_name().or(correct_address());
    assert!(predicate.satisfied(&test_peer()));

    let predicate = incorrect_name().or(correct_address());
    assert!(predicate.satisfied(&test_peer()));

    let predicate = correct_name().or(incorrect_address());
    assert!(predicate.satisfied(&test_peer()));
}

#[test]
fn predicate_not_incorrect_succeeds() {
    let predicate = incorrect_name().not();
    assert!(predicate.satisfied(&test_peer()));
}

#[test]
fn predicate_not_correct_fails() {
    let predicate = correct_name().not();
    assert!(!predicate.satisfied(&test_peer()));
}

#[test]
fn over_simple_incorrect_predicate_fail() -> Result<(),AssertionText> {
    let p = Predicate::equal(Some("INCORRECT_NAME".to_string()));
    //let predicate = focus!(RemoteDevice, p, peer => peer.name.clone());
    let predicate = p.over(|p: &RemoteDevice| p.name.clone(), ".name");
    let r = predicate.satisfied_(&test_peer());
    //if let Err(e) = &r {
        //println!("{}", e);
    //};
    r
}

#[test]
fn over_simple_incorrect_not_predicate_fail() -> Result<(),AssertionText> {
    let p = Predicate::not(Predicate::equal(Some(TEST_PEER_NAME.to_string())));
    //let predicate = focus!(RemoteDevice, p, peer => peer.name.clone());
    let predicate = p.over(|p: &RemoteDevice| p.name.clone(), ".name");
    let r = predicate.satisfied_(&test_peer());
    //if let Err(e) = &r {
        //println!("{}", e);
    //};
    r
}


/*
 * OVER
 *   .name
 * EXPECTED
 *   Foo
 * FOUND
 *   Bar
 *
 *
 *
 *
 *
 */
