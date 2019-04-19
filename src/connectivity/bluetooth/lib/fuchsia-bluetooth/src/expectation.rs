// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::{
        fmt::{self, Debug, Formatter},
        marker::PhantomData,
        sync::Arc,
    },
    //pretty::{BoxAllocator, DocAllocator, DocBuilder},
    pretty::{BoxAllocator, DocAllocator},
};

/// Asynchronous extensions to Expectation Predicates
pub mod asynchronous;
/// Expectations for the host driver
pub mod host_driver;
/// Expectations for remote peers
pub mod peer;
/// Tests for the expectation module
#[cfg(test)]
pub mod test;

/*
pub struct OldPredicate<T> {
    inner: Arc<dyn Fn(&T) -> bool + Send + Sync + 'static>,
    /// A descriptive piece of text used for debug printing via `{:?}`
    description: String,
}
*/

/// A String whose `Debug` implementation pretty-prints
#[derive(Clone)]
pub struct AssertionText(String);

impl fmt::Debug for AssertionText {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

// A function for producing a pretty-printed Doc
//type DescFn = Arc<dyn for<'a> Fn(&'a BoxAllocator) -> DocBuilder<'a> + Send + Sync + 'static>;

type CompFn<T> = Arc<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;
type ReprFn<T> = Arc<dyn Fn(&T) -> String + Send + Sync + 'static>;

/// A Boolean predicate on type `T`. Predicate functions are a boolean algebra
/// just as raw boolean values are; they an be ANDed, ORed, NOTed. This allows
/// a clear and concise language for declaring test expectations.
#[derive(Clone)]
pub enum Predicate<T> {
    Equal(T, CompFn<T>, ReprFn<T>),
    And(Box<Predicate<T>>, Box<Predicate<T>>),
    Or(Box<Predicate<T>>, Box<Predicate<T>>),
    Not(Box<Predicate<T>>),
    Predicate(Arc<dyn Fn(&T) -> bool + Send + Sync + 'static>, String),
    // instead of: Over(Predicate<U>, Fn(&T)->&U) for<U>
    Over(Arc<dyn IsOver<T> + Send + Sync + 'static>),
    // instead of: Any(Fn(&T) -> I, Predicate<I::Elem>) for<I> where I::Elem: Debug
    Any(Arc<dyn IsAny<T> + Send + Sync + 'static>),
    // instead of: All(Fn(&T) -> I, Predicate<I::Elem>) for<I> where I::Elem: Debug
    All(Arc<dyn IsAll<T> + Send + Sync + 'static>),
}


/// Trait representation of OverPred where `U` is existential
pub trait IsOver<T> {
    fn apply(&self, t: &T) -> bool;
    fn desc(&self) -> String;
    fn name(&self) -> String;
    fn falsify(&self, t: &T) -> Option<String>;
}

/// Trait representation of AnyPred where `Iter` is existential
pub trait IsAny<T: 'static> {
    fn apply(&self, t: &T) -> bool;
    fn desc(&self) -> String;
    fn falsify(&self, t: &T) -> Option<String>;
}

struct OverPred<T,U> {
    pred: Predicate<U>,
    project: Arc<dyn Fn(&T) -> &U + Send + Sync + 'static>,
    path: String,
}

impl<T, U: 'static> IsOver<T> for OverPred<T,U> {
    fn apply(&self, t: &T) -> bool {
        self.pred.satisfied((self.project)(t))
    }
    fn desc(&self) -> String {
        self.pred.describe()
    }
    fn name(&self) -> String {
        self.path.clone()
    }
    fn falsify(&self, t: &T) -> Option<String> {
        match self.pred.falsify((self.project)(t)) {
            Some(s) => Some(format!("{} {}", self.name(), s)),
            None => None,
        }
    }
}

/// At most how many elements of an iterator to show in a falsification, when falsifying `any` or
/// `all`
const MAX_ITER_FALSIFICATIONS: usize = 5;

struct AnyPred<T, Elem>
where for<'a> &'a T: IntoIterator<Item = &'a Elem> {
    pred: Predicate<Elem>,
    _phantom: PhantomData<T>
}


impl<T: 'static, Elem: 'static> IsAny<T> for AnyPred<T,Elem>
where for<'a> &'a T: IntoIterator<Item = &'a Elem> {
    fn apply(&self, t: &T) -> bool {
        t.into_iter().any(|i| self.pred.satisfied(i))
    }
    fn desc(&self) -> String {
        format!("ANY {}", self.pred.describe())
    }
    fn falsify(&self, t: &T) -> Option<String> {
        // TODO(nickpollard) - Is this right?
        t.into_iter()
            .filter_map(|i| self.pred.falsify(i))
            .take(MAX_ITER_FALSIFICATIONS)
            .fold(None, |acc, falsification| Some(format!("{}{}",acc.unwrap_or("".to_string()), falsification)))
    }
}

struct AllPred<T, Elem>
where for<'a> &'a T: IntoIterator<Item = &'a Elem> {
    pred: Predicate<Elem>,
    _phantom: PhantomData<T>
}

/// Trait representation of AllPred where `Iter` is existential
pub trait IsAll<T: 'static> {
    fn apply(&self, t: &T) -> bool;
    fn desc(&self) -> String;
    fn falsify(&self, t: &T) -> Option<String>;
}

impl<T: 'static, Elem: Debug + 'static> IsAll<T> for AllPred<T,Elem>
where for<'a> &'a T: IntoIterator<Item = &'a Elem> {
    fn apply(&self, t: &T) -> bool {
        t.into_iter().all(|i| self.pred.satisfied(i))
    }
    fn desc(&self) -> String {
        format!("ALL {}", self.pred.describe())
    }
    fn falsify(&self, t: &T) -> Option<String> {
        let failures = t.into_iter()
            .filter_map(|i| self.pred.falsify(i).map(|msg| format!("ELEM {:?} FAILS: {}", i, msg)))
            .take(MAX_ITER_FALSIFICATIONS)
            .fold(None, |acc, falsification| Some(format!("{}{}",acc.unwrap_or("".to_string()), falsification)));
        failures.map(|msg| format!("FAILED\n\t{}\nDUE TO\n\t{}", self.desc(), msg))
    }
}

impl<T: PartialEq + Debug + 'static> Predicate<T> {
    fn equal(t: T) -> Predicate<T> {
        Predicate::Equal(t, Arc::new(T::eq), Arc::new(|t| format!("{:?}", t)))
    }
}

impl<T> Predicate<T>
where T: 'static {
    pub fn satisfied(&self, t: &T) -> bool {
        match self {
            Predicate::Equal(expected, are_eq, _) => are_eq(t, expected),
            Predicate::And(left, right) => left.satisfied(t) && right.satisfied(t),
            Predicate::Or(left, right) => left.satisfied(t) || right.satisfied(t),
            Predicate::Not(inner) => !inner.satisfied(t),
            Predicate::Predicate(pred, _) => pred(t),
            Predicate::Over(over) => over.apply(t),
            Predicate::Any(any) => any.apply(t),
            Predicate::All(all) => all.apply(t),
        }
    }
    pub fn describe(&self) -> String {
        match self {
            Predicate::Equal(expected, _, repr) => format!("EQUAL {}", repr(expected)),
            Predicate::And(left, right) => format!("({}) AND ({})", left.describe(), right.describe()),
            Predicate::Or(left, right) => format!("({}) OR ({})", left.describe(), right.describe()),
            Predicate::Not(inner) => format!("NOT {}", inner.describe()),
            Predicate::Predicate(_, desc) => desc.clone(),
            Predicate::Over(over) => format!("{} {}", over.name(), over.desc()),
            Predicate::Any(any) => any.desc(),
            Predicate::All(all) => all.desc(),
        }
    }
    /// Provide a minimized falsification of the predicate, if possible
    pub fn falsify(&self, t: &T) -> Option<String> {
        match self {
            Predicate::Equal(expected, are_eq, repr) => {
                if are_eq(expected, t) {
                    None
                } else {
                    Some(format!("{} != {}", repr(t), repr(expected)))
                }
            },
            Predicate::And(left, right) => {
                match (left.falsify(t), right.falsify(t)) {
                    (Some(l), Some(r)) => Some(format!("{}\n{}", l, r)),
                    (Some(l), None) => Some(l),
                    (None, Some(r)) => Some(r),
                    (None, None) => None
                }
            }
            Predicate::Or(left, right) => {
                match (left.falsify(t), right.falsify(t)) {
                    (Some(l), Some(r)) => Some(format!("({}) AND ({})", l, r)),
                    (Some(_), None) => None,
                    (None, Some(_)) => None,
                    (None, None) => None
                }
            }
            Predicate::Not(inner) => {
                match inner.falsify(t) {
                    Some(_) => None,
                    None => Some(format!("NOT {}", inner.describe())),
                }
            },
            Predicate::Predicate(pred, desc) => {
                if pred(t) {
                    None
                } else {
                    Some(desc.to_string())
                }
            },
            Predicate::Over(over) => over.falsify(t),
            Predicate::Any(any) => any.falsify(t),
            Predicate::All(all) => all.falsify(t),
        }
    }
    pub fn satisfied_(&self, t: &T) -> Result<(),AssertionText> {
        match self.falsify(t) {
            Some(falsification) => Err(AssertionText(format!("FAILED EXPECTATION\n\t{}\nFALSIFIED BY\n\t{}", self.describe(), falsification))),
            None => Ok(()),
        }
    }
}

impl<T> Predicate<T> {
    pub fn and(self, rhs: Predicate<T>) -> Predicate<T> {
        Predicate::And(Box::new(self), Box::new(rhs))
    }
    pub fn or(self, rhs: Predicate<T>) -> Predicate<T> {
        Predicate::Or(Box::new(self), Box::new(rhs))
    }
    pub fn not(self) -> Predicate<T> {
        Predicate::Not(Box::new(self))
    }
    pub fn new<F, S>(f: F, label: S) -> Predicate<T>
    where
        F: Fn(&T) -> bool + Send + Sync + 'static,
        S: Into<String>
    {
        Predicate::Predicate(Arc::new(f), label.into())
    }
}

/// Methods to work with `T`s that are some collection of elements `Elem`.
impl<Elem, T: Send + Sync + 'static> Predicate<T>
where for<'a> &'a T: IntoIterator<Item = &'a Elem>,
    Elem: Debug + Send + Sync + 'static {

    pub fn all(pred: Predicate<Elem>) -> Predicate<T> {
        Predicate::All(Arc::new(AllPred{ pred, _phantom: PhantomData }))
    }

    pub fn any(pred: Predicate<Elem>) -> Predicate<T> {
        Predicate::Any(Arc::new(AnyPred{ pred, _phantom: PhantomData }))
    }
}

impl<T: Send + Sync + 'static> Predicate<T> {
    pub fn over<F, A: 'static, S>(self, project: F, path: S) -> Predicate<A>
    where F: Fn(&A) -> &T + Send + Sync + 'static,
          S: Into<String> {
        Predicate::Over(Arc::new(OverPred {
            pred: self,
            project: Arc::new(project),
            path: path.into(),
        }))
    }
}

/*

impl<T> Clone for OldPredicate<T> {
    fn clone(&self) -> OldPredicate<T> {
        OldPredicate { inner: self.inner.clone(), description: self.description.clone() }
    }
}

impl<T> Debug for OldPredicate<T> {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "{}", self.description)
    }
}

impl<T: 'static> OldPredicate<T> {
    pub fn satisfied_(&self, t: &T) -> Result<(),AssertionText> {
        if (self.inner)(t) {
            Ok(())
        } else {
            Err(AssertionText(self.describe()))
        }
    }
    pub fn satisfied(&self, t: &T) -> bool {
        (self.inner)(t)
    }
    pub fn or(self, rhs: OldPredicate<T>) -> OldPredicate<T> {
        let description = format!("({}) OR ({})", self.description, rhs.description);
        OldPredicate {
            inner: Arc::new(move |t: &T| -> bool { (self.inner)(t) || (rhs.inner)(t) }),
            description,
        }
    }
    pub fn and(self, rhs: OldPredicate<T>) -> OldPredicate<T> {
        let description = format!("({}) AND ({})", self.description, rhs.description);
        OldPredicate {
            inner: Arc::new(move |t: &T| -> bool { (self.inner)(t) && (rhs.inner)(t) }),
            description,
        }
    }
    pub fn not(self) -> OldPredicate<T> {
        let description = format!("NOT ({})", self.description);
        OldPredicate { inner: Arc::new(move |t: &T| -> bool { !(self.inner)(t) }), description }
    }

    pub fn new<F>(f: F, label: Option<&str>) -> OldPredicate<T>
    where
        F: Fn(&T) -> bool + Send + Sync + 'static,
    {
        OldPredicate {
            inner: Arc::new(f),
            description: label.unwrap_or("<Unrepresentable OldPredicate>").to_string(),
        }
    }

    pub fn describe(&self) -> String {
        self.description.clone()
    }
}

impl<B: 'static> OldPredicate<B> {
    pub fn over<F, A, S>(self, project: F, path: S) -> OldPredicate<A>
    where F: Fn(&A) -> B + Send + Sync + 'static,
          S: Into<String> {
        let inner = self.inner;
        let description = self.description;
        OldPredicate {
            inner: Arc::new(move |t| (inner)(&project(t))),
            description: format!("OVER\n\t{}\nEXPECTED\n\t{}", path.into(), description)
        }
    }
}

impl<T: PartialEq + Debug + Send + Sync + 'static> OldPredicate<T> {
    pub fn equal(expected: T) -> OldPredicate<T> {
        let description = format!("Equal to {:?}", expected);
        OldPredicate{ inner: Arc::new(move |t| t == &expected), description }
    }
}
*/

impl<T: 'static> Predicate<T> {
    /*
    pub fn and(self, rhs: Predicate<T>) -> Predicate<T> {
        //let description = format!("({}) AND ({})", self.description, rhs.description);
        let self_desc = self.desc.clone();
        let rhs_desc = rhs.desc.clone();
        Predicate {
            inner: Arc::new(move |t: &T| -> bool { (self.inner)(t) && (rhs.inner)(t) }),
            desc: Arc::new(move |alloc| {
                alloc.text("(")
                    .append((self_desc)(alloc))
                    .append(alloc.text(")"))
                    .append(alloc.text("AND"))
                    .append(alloc.text("("))
                    .append((rhs_desc)(alloc))
                    .append(alloc.text(")"))
            }),
        }
    }
    */
}

#[macro_export]
macro_rules! focus {
    ($type:ty, $pred:ident, $var:ident => $selector:expr) => {
        $pred.over(|$var: &$type| $selector, stringify!($selector).to_string())
    }
}
type DocBuilder<'a> = pretty::DocBuilder<'a, BoxAllocator>;

pub fn desc<'a>(alloc: &'a BoxAllocator) -> DocBuilder<'a> {
    alloc.text("foo").append(alloc.newline())
}
