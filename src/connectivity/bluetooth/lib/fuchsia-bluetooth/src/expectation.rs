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

/// A Boolean predicate on type `T`. Predicate functions are a boolean algebra
/// just as raw boolean values are; they an be ANDed, ORed, NOTed. This allows
/// a clear and concise language for declaring test expectations.
pub struct Predicate<T> {
    inner: Arc<dyn Fn(&T) -> bool + Send + Sync + 'static>,
    /// A descriptive piece of text used for debug printing via `{:?}`
    description: String,
}

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

/*
pub struct Pred<T> {
    inner: Arc<dyn Fn(&T) -> bool + Send + Sync + 'static>,

    desc: DescFn,
}
*/

struct OverPred<T,U> {
    pred: Pred<U>,
    project: Arc<dyn Fn(&T) -> &U + Send + Sync + 'static>,
    path: String,
}

// Trait representation of OverPred where `U` is existential
pub trait IsOver<T> {
    fn apply(&self, t: &T) -> bool;
    fn desc(&self) -> String;
    fn name(&self) -> String;
    fn falsify(&self, t: &T) -> Option<String>;
}

impl<T, U: PartialEq + Debug> IsOver<T> for OverPred<T,U> {
//    type U_ = U;
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
    pred: Pred<Elem>,
    _phantom: PhantomData<T>
}

// Trait representation of AnyPred where `Iter` is existential
pub trait IsAny<T> {
    fn apply(&self, t: &T) -> bool;
    fn desc(&self) -> String;
    fn falsify(&self, t: &T) -> Option<String>;
}

impl<T, Elem> IsAny<T> for AnyPred<T,Elem>
where for<'a> &'a T: IntoIterator<Item = &'a Elem>,
    Elem: PartialEq + Debug {
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
    pred: Pred<Elem>,
    _phantom: PhantomData<T>
}

// Trait representation of AllPred where `Iter` is existential
pub trait IsAll<T> {
    fn apply(&self, t: &T) -> bool;
    fn desc(&self) -> String;
    fn falsify(&self, t: &T) -> Option<String>;
}

impl<T, Elem> IsAll<T> for AllPred<T,Elem>
where for<'a> &'a T: IntoIterator<Item = &'a Elem>,
    Elem: PartialEq + Debug {
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

#[derive(Clone)]
pub enum Pred<T> {
    Equal(T),
    And(Arc<Pred<T>>, Arc<Pred<T>>),
    Or(Arc<Pred<T>>, Arc<Pred<T>>),
    Not(Arc<Pred<T>>),
    Pred(Arc<dyn Fn(&T) -> bool + Send + Sync + 'static>, String),
    // instead of: Over(Pred<U>, Fn(&T)->&U) for<U>
    Over(Arc<dyn IsOver<T>>),
    // instead of: Any(Fn(&T) -> I, Pred<I::Elem>) for<I> where I::Elem: Debug
    Any(Arc<dyn IsAny<T>>),
    // instead of: All(Fn(&T) -> I, Pred<I::Elem>) for<I> where I::Elem: Debug
    All(Arc<dyn IsAll<T>>),
}

impl<T: PartialEq + Debug> Pred<T> {
    pub fn satisfied(&self, t: &T) -> bool {
        match self {
            Pred::Equal(expected) => *t == *expected,
            Pred::And(left, right) => left.satisfied(t) && right.satisfied(t),
            Pred::Or(left, right) => left.satisfied(t) || right.satisfied(t),
            Pred::Not(inner) => !inner.satisfied(t),
            Pred::Pred(pred, _) => pred(t),
            Pred::Over(over) => over.apply(t),
            Pred::Any(any) => any.apply(t),
            Pred::All(all) => all.apply(t),
        }
    }

    pub fn describe(&self) -> String {
        match self {
            Pred::Equal(expected) => format!("EQUAL {:?}", expected),
            Pred::And(left, right) => format!("({}) AND ({})", left.describe(), right.describe()),
            Pred::Or(left, right) => format!("({}) OR ({})", left.describe(), right.describe()),
            Pred::Not(inner) => format!("NOT {}", inner.describe()),
            Pred::Pred(_, desc) => desc.clone(),
            Pred::Over(over) => format!("{} {}", over.name(), over.desc()),
            Pred::Any(any) => any.desc(),
            Pred::All(all) => all.desc(),
        }
    }

    /// Provide a minimized falsification of the predicate, if possible
    pub fn falsify(&self, t: &T) -> Option<String> {
        match self {
            Pred::Equal(expected) => {
                if *expected == *t {
                    None
                } else {
                    Some(format!("{:?} != {:?}", t, expected))
                }
            },
            Pred::And(left, right) => {
                match (left.falsify(t), right.falsify(t)) {
                    (Some(l), Some(r)) => Some(format!("{}\n{}", l, r)),
                    (Some(l), None) => Some(l),
                    (None, Some(r)) => Some(r),
                    (None, None) => None
                }
            }
            Pred::Or(left, right) => {
                match (left.falsify(t), right.falsify(t)) {
                    (Some(l), Some(r)) => Some(format!("({}) AND ({})", l, r)),
                    (Some(_), None) => None,
                    (None, Some(_)) => None,
                    (None, None) => None
                }
            }
            Pred::Not(inner) => {
                match inner.falsify(t) {
                    Some(_) => None,
                    None => Some(format!("NOT {}", inner.describe())),
                }
            },
            Pred::Pred(pred, desc) => {
                if pred(t) {
                    None
                } else {
                    Some(desc.to_string())
                }
            },
            Pred::Over(over) => over.falsify(t),
            Pred::Any(any) => any.falsify(t),
            Pred::All(all) => all.falsify(t),
        }
    }
    pub fn satisfied_(&self, t: &T) -> Result<(),AssertionText> {
        match self.falsify(t) {
            Some(falsification) => Err(AssertionText(format!("FAILED EXPECTATION\n\t{}\nFALSIFIED BY\n\t{}", self.describe(), falsification))),
            None => Ok(()),
        }
    }
}

impl<Elem, T> Pred<T>
where for<'a> &'a T: IntoIterator<Item = &'a Elem>,
    Elem: PartialEq + Debug + 'static,
    T: 'static {

    pub fn all(pred: Pred<Elem>) -> Pred<T> {
        Pred::All(Arc::new(AllPred{ pred, _phantom: PhantomData }))
    }

    pub fn any(pred: Pred<Elem>) -> Pred<T> {
        Pred::Any(Arc::new(AnyPred{ pred, _phantom: PhantomData }))
    }
}

impl<T: Clone> Pred<T> {
    pub fn or(self, rhs: Pred<T>) -> Pred<T> {
        Pred::Or(Arc::new(self.clone()), Arc::new(rhs.clone()))
    }
    pub fn and(self, rhs: Pred<T>) -> Pred<T> {
        Pred::And(Arc::new(self.clone()), Arc::new(rhs.clone()))
    }
    pub fn not(&self) -> Pred<T> {
        Pred::Not(Arc::new(self.clone()))
    }

    /*
    pub fn all<F, A>(self, project: F) -> Predicate<A>
    where F: Fn(&A) -> B + Send + Sync + 'static {
        let inner = self.inner;
        let description = self.description;
        Predicate {
            inner: Arc::new(move |t| (inner)(&project(t))),
            description: format!("OVER\n\t{}\nEXPECTED\n\t{}", path.into(), description)
        }
    }
    */

    // TODO(nickpollard) - don't take Option
    pub fn new<F>(f: F, label: Option<&str>) -> Pred<T>
    where
        F: Fn(&T) -> bool + Send + Sync + 'static,
    {
        Pred::Pred(Arc::new(f), label.unwrap_or("<Unrepresentable Predicate>").to_string())
    }
}

impl<B: PartialEq + Debug + 'static> Pred<B> {
    pub fn over<F, A: 'static, S>(self, project: F, path: S) -> Pred<A>
    where F: Fn(&A) -> &B + Send + Sync + 'static,
          S: Into<String> {
        Pred::Over(Arc::new(OverPred {
            pred: self,
            project: Arc::new(project),
            path: path.into(),
        }))
    }
}

impl<T> Clone for Predicate<T> {
    fn clone(&self) -> Predicate<T> {
        Predicate { inner: self.inner.clone(), description: self.description.clone() }
    }
}

impl<T> Debug for Predicate<T> {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "{}", self.description)
    }
}

impl<T: 'static> Predicate<T> {
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
    pub fn or(self, rhs: Predicate<T>) -> Predicate<T> {
        let description = format!("({}) OR ({})", self.description, rhs.description);
        Predicate {
            inner: Arc::new(move |t: &T| -> bool { (self.inner)(t) || (rhs.inner)(t) }),
            description,
        }
    }
    pub fn and(self, rhs: Predicate<T>) -> Predicate<T> {
        let description = format!("({}) AND ({})", self.description, rhs.description);
        Predicate {
            inner: Arc::new(move |t: &T| -> bool { (self.inner)(t) && (rhs.inner)(t) }),
            description,
        }
    }
    pub fn not(self) -> Predicate<T> {
        let description = format!("NOT ({})", self.description);
        Predicate { inner: Arc::new(move |t: &T| -> bool { !(self.inner)(t) }), description }
    }

    pub fn new<F>(f: F, label: Option<&str>) -> Predicate<T>
    where
        F: Fn(&T) -> bool + Send + Sync + 'static,
    {
        Predicate {
            inner: Arc::new(f),
            description: label.unwrap_or("<Unrepresentable Predicate>").to_string(),
        }
    }

    pub fn describe(&self) -> String {
        self.description.clone()
    }
}

impl<B: 'static> Predicate<B> {
    pub fn over<F, A, S>(self, project: F, path: S) -> Predicate<A>
    where F: Fn(&A) -> B + Send + Sync + 'static,
          S: Into<String> {
        let inner = self.inner;
        let description = self.description;
        Predicate {
            inner: Arc::new(move |t| (inner)(&project(t))),
            description: format!("OVER\n\t{}\nEXPECTED\n\t{}", path.into(), description)
        }
    }
}

impl<T: PartialEq + Debug + Send + Sync + 'static> Predicate<T> {
    pub fn equal(expected: T) -> Predicate<T> {
        let description = format!("Equal to {:?}", expected);
        Predicate{ inner: Arc::new(move |t| t == &expected), description }
    }
}

impl<T: 'static> Pred<T> {
    /*
    pub fn and(self, rhs: Pred<T>) -> Pred<T> {
        //let description = format!("({}) AND ({})", self.description, rhs.description);
        let self_desc = self.desc.clone();
        let rhs_desc = rhs.desc.clone();
        Pred {
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

//($body:ident, $signal:ident, $peer:ident, $id:ident, $request_variant:ident, $responder_type:ident) => {
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
