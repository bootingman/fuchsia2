// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::WriteInspect;

use crate::NodeExt;

use fidl_fuchsia_inspect as fidl_inspect;
use fuchsia_inspect as finspect;

pub struct InspectBytes<'a>(pub &'a [u8]);

impl<'a> WriteInspect for InspectBytes<'a> {
    fn write_inspect(&self, node: &mut finspect::ObjectTreeNode, key: &str) {
        node.add_property(fidl_inspect::Property {
            key: key.to_string(),
            value: fidl_inspect::PropertyValue::Bytes(self.0.to_vec()),
        });
    }
}

pub struct InspectList<'a, T>(pub &'a [T]);

impl<'a, T> WriteInspect for InspectList<'a, T>
where
    T: WriteInspect,
{
    fn write_inspect(&self, node: &mut finspect::ObjectTreeNode, key: &str) {
        let child = node.create_child(key);
        let mut child = child.lock();
        for (i, val) in self.0.iter().enumerate() {
            child.insert(&i.to_string(), val);
        }
    }
}

/// Wrapper around a list `[T]` and a closure function `F` that determines how to map
/// and log each value of `T`.
pub struct InspectListClosure<'a, T, F>(pub &'a [T], pub F)
where
    F: Fn(&mut finspect::ObjectTreeNode, &str, &T);

impl<'a, T, F> WriteInspect for InspectListClosure<'a, T, F>
where
    F: Fn(&mut finspect::ObjectTreeNode, &str, &T),
{
    fn write_inspect(&self, node: &mut finspect::ObjectTreeNode, key: &str) {
        let child = node.create_child(key);
        let mut child = child.lock();
        for (i, val) in self.0.iter().enumerate() {
            self.1(&mut child, &i.to_string(), val);
        }
    }
}
