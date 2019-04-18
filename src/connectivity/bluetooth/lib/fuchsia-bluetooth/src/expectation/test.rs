//use crate::focus;
use crate::expectation::*;
use crate::expectation::Predicate as P;
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
    let predicate = Predicate::equal(Some(TEST_PEER_NAME.to_string())).over(
            |dev: &RemoteDevice| &dev.name,
            ".name");
    assert!(predicate.satisfied(&test_peer()));
}
#[test]
fn simple_incorrect_predicate_fail() {
    let predicate = Predicate::equal(Some(INCORRECT_PEER_NAME.to_string())).over(
            |dev: &RemoteDevice| &dev.name,
            ".name");
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
    let predicate = P::equal(Some("INCORRECT_NAME".to_string()))
        .over(|p: &RemoteDevice| &p.name, ".name");
    predicate.satisfied_(&test_peer())
}

#[test]
fn over_simple_incorrect_not_predicate_fail() -> Result<(),AssertionText> {
    let p = P::not(P::equal(Some(TEST_PEER_NAME.to_string())));
    let predicate = p.over(|p: &RemoteDevice| &p.name, ".name");
    predicate.satisfied_(&test_peer())
}

#[derive(Debug, PartialEq, Clone)]
struct Person {
    name: String,
    age: u64,
}

#[derive(Debug, PartialEq, Clone)]
struct Group {
    persons: Vec<Person>,
}

#[test]
fn persons() -> Result<(),AssertionText> {
    let test_group = Group {
        persons: vec![Person{ name: "Larry".to_string(), age: 40 },
                      Person{ name: "Sergei".to_string(), age: 41 }]
    };

    let p = P::<Vec<Person>>::all(P::not(P::equal("Sergei".to_string())).over(|p: &Person| &p.name, ".name")
                                     .and(P::new(|age: &u64| *age < 50, "< 50").over(|p: &Person| &p.age, ".age")));
    let predicate = p.over(|p: &Group| &p.persons, ".persons");
    predicate.satisfied_(&test_group)
}
