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
}

struct AnyPred<T, Elem>
where for<'a> &'a T: IntoIterator<Item = &'a Elem> {
    pred: Pred<Elem>,
    _phantom: PhantomData<T>
    //project: Arc<dyn Fn(&T) -> &Iter + Send + Sync + 'static>,
}

// Trait representation of AnyPred where `Iter` is existential
pub trait IsAny<T> {
    fn apply(&self, t: &T) -> bool;
    fn desc(&self) -> String;
}

pub enum Pred<T> {
    Equal(T),
    And(Arc<Pred<T>>, Arc<Pred<T>>),
    Or(Arc<Pred<T>>, Arc<Pred<T>>),
    Not(Arc<Pred<T>>),
    Pred(Arc<dyn Fn(&T) -> bool + Send + Sync + 'static>, String),
    // instead of: Over(Pred<U>, Fn(&T)->&U), // for some U
    Over(Arc<dyn IsOver<T>>), // for some U
    // instead of: Any(Fn(&T) -> I, Pred<I::Elem>), // for some I, where I::Elem : Debug
    Any(Arc<dyn IsAny<T>>),
}

impl<T: PartialEq + Debug> Pred<T> {
    pub fn satisfied(&self, t: &T) -> bool {
        match self {
            Pred::Equal(expected) => *t == *expected,
            Pred::And(left, right) => left.satisfied(t) && right.satisfied(t),
            Pred::Or(left, right) => left.satisfied(t) || right.satisfied(t),
            Pred::Not(inner) => !inner.satisfied(t),
            _ => panic!("NYI"),
        }
    }

    pub fn describe(&self) -> String {
        match self {
            Pred::Equal(expected) => format!("Expected {:?}", expected),
            Pred::And(left, right) => format!("\t{:?}\nAND\n\t{:?}", left.describe(), right.describe()),
            Pred::Or(left, right) => format!("\t{:?}\nOR\n\t{:?}", left.describe(), right.describe()),
            Pred::Not(inner) => format!("NOT\n\t{:?}", inner.describe()),
            Pred::Pred(_, desc) => desc.clone(),
            Pred::Over(over) => format!("OVER\n\t{:?}\nPROJECT\n\t{:?}", over.name(), over.desc()),
            Pred::Any(any) => format!("ANY\n\t{:?}", any.desc()),
        }
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

//impl<T: PartialEq + Debug + Send + Sync + 'static> Pred<T> {
//}

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
