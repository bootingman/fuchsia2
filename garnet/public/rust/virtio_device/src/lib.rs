// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, futures_api, integer_atomics, await_macro)]
#![allow(warnings)]

pub mod ring {

    use std::{
        mem, slice,
        sync::atomic::{AtomicU16, Ordering},
    };

    /// This marks a buffer as continuing via the next field.
    const VRING_DESC_F_NEXT: u16 = 1;
    /// This marks a buffer as device write-only (otherwise device read-only).
    const VRING_DESC_F_WRITE: u16 = 2;
    /// This means the buffer contains a list of buffer descriptors.
    const VRING_DESC_F_INDIRECT: u16 = 4;

    #[repr(C)]
    #[derive(Debug, Clone)]
    pub struct Desc {
        addr: u64,
        len: u32,
        // This is not bitflags! as it may contain additional bits that we do not define
        // and so would violate the bitflags type safety.
        flags: u16,
        next: u16,
    }

    impl Desc {
        pub fn has_next(&self) -> bool {
            self.flags & VRING_DESC_F_NEXT != 0
        }
        pub fn is_indirect(&self) -> bool {
            self.flags & VRING_DESC_F_INDIRECT != 0
        }
        pub fn write_only(&self) -> bool {
            self.flags & VRING_DESC_F_WRITE != 0
        }
        pub fn next(&self) -> Option<u16> {
            if self.has_next() {
                Some(self.next)
            } else {
                None
            }
        }
        pub fn data(&self) -> (u64, u32) {
            (self.addr, self.len)
        }
    }

    #[repr(C)]
    #[derive(Debug)]
    pub struct Header {
        flags: u16,
        idx: AtomicU16,
    }

    #[repr(C)]
    #[derive(Debug, Clone, Copy)]
    pub struct Used {
        /// Index of start of used descriptor chain.
        ///
        /// For padding reasons `id` in this structure is 32-bits, although it will never exceed
        /// an actual 16-bit descriptor index.
        id: u32,
        /// Total length of the descriptor chain which was used (written to)
        len: u32,
    }

    impl Used {
        pub fn new(id: u16, len: u32) -> Used {
            Used { id: id.into(), len }
        }
    }

    // This is not the hardware layout. Is a 'minimal' rust definition of the layout
    // of the memory of a ring. Technically the 'minimal' layout is 3 pointers and
    // a size, but this is a little more ergonomic for a bit of extra memory.
    // This is split into the host read only and guest read only portions
    // Represents the Driver owned data.
    pub struct Driver {
        desc: &'static [Desc],
        avail_header: &'static Header,
        avail: &'static [u16],
        used_event_index: &'static u16,
    }

    impl Driver {
        pub fn get_avail(&self, next_index: u16) -> Option<u16> {
            if next_index != self.avail_header.idx.load(Ordering::Acquire) {
                let index = self.avail.get(next_index as usize % self.avail.len()).unwrap();
                Some(*index)
            } else {
                None
            }
        }
        pub fn get_desc(&self, index: u16) -> Option<&'static Desc> {
            self.desc.get(index as usize)
        }
        /// How many bytes the driver portion of the ring should be for the given count
        pub fn avail_len_for_count(count: u16) -> usize {
            mem::size_of::<Header>() + mem::size_of::<u16>() * (count as usize + 1)
        }
        pub fn count(&self) -> u16 {
            self.avail.len() as u16
        }
        // Returns None if desc and avail are miss sized
        pub fn new(desc: &'static [u8], avail: &'static [u8]) -> Option<Self> {
            let count = desc.len() / mem::size_of::<Desc>();
            if !count.is_power_of_two() {
                return None;
            }

            let desc = unsafe { slice::from_raw_parts(desc.as_ptr() as *const Desc, count) };

            if avail.len() < mem::size_of::<Header>() {
                return None;
            }
            let (avail_header, rest) = avail.split_at(mem::size_of::<Header>());
            let avail_header = unsafe { &*(avail_header.as_ptr() as *const Header) };

            // Reinterpret the rest as a [u16], with the last one being the used_event_index
            if rest.len() != mem::size_of::<u16>() * (count + 1) {
                return None;
            }
            let rest = unsafe { slice::from_raw_parts(rest.as_ptr() as *const u16, count + 1) };
            let (used_event_index, avail) = rest.split_last().unwrap();
            Some(Self { desc, avail_header, avail, used_event_index })
        }
        pub fn needs_interrupt(&self, feature_event_idx: bool, submitted: u16) -> bool {
            if feature_event_idx {
                submitted == *self.used_event_index
            } else {
                self.avail_header.flags == 0
            }
        }
    }

    // Represents the device owned data.
    pub struct Device {
        used_header: &'static mut Header,
        used: &'static mut [Used],
        avail_event_index: &'static mut u16,
    }

    impl Device {
        pub fn used_len_for_count(count: u16) -> usize {
            mem::size_of::<Header>()
                + mem::size_of::<Used>() * count as usize
                + mem::size_of::<u16>()
        }
        pub fn count(&self) -> u16 {
            self.used.len() as u16
        }
        // Returns None if slice miss sized
        pub fn new(used: &'static mut [u8]) -> Option<Self> {
            if used.len() < mem::size_of::<Header>() {
                return None;
            }
            let (used_header, rest) = used.split_at_mut(mem::size_of::<Header>());
            let used_header = unsafe { &mut *(used_header.as_mut_ptr() as *mut Header) };

            // Take the last u16 from what is remaining as avail_event_index
            if rest.len() < mem::size_of::<u16>() {
                return None;
            }
            let (used, avail_event_index) = rest.split_at_mut(rest.len() - mem::size_of::<u16>());
            let avail_event_index = unsafe { &mut *(avail_event_index.as_mut_ptr() as *mut u16) };

            // Reinterpret used as [Used]
            let used = unsafe {
                slice::from_raw_parts_mut(
                    used.as_mut_ptr() as *mut Used,
                    used.len() / mem::size_of::<Used>(),
                )
            };
            if !used.len().is_power_of_two() {
                return None;
            }

            Some(Self { used_header, used, avail_event_index })
        }
        /// # Panics
        ///
        /// Will panic if the index being written is not a valid index in the Used ring.
        pub fn insert_used(&mut self, used: Used, index: u16) {
            let used_slot = self
                .used
                .get_mut(index as usize % self.used.len())
                .expect("Returned invalid index");
            *used_slot = used;
        }
        pub fn publish_used(&mut self, index: u16) {
            self.used_header.idx.store(index, Ordering::Release);
        }
    }
}

mod queue {
    use crate::ring;
    use {
        crossbeam::crossbeam_channel,
        failure::Error,
        parking_lot::Mutex,
        std::{
            mem,
            ops::{Deref, DerefMut},
            sync::atomic,
        },
    };

    // Mutable state for the rings.
    struct RingState {
        device_state: ring::Device,
        // Next index in avail that we expect to be come available
        next: u16,
        // Next index in used that we will publish at
        next_used: u16,
    }

    impl RingState {
        fn return_chain(&mut self, desc: u16, written: u32) -> u16 {
            let submit = self.next_used;
            self.device_state.insert_used(ring::Used::new(desc, written), submit);
            self.next_used = submit.wrapping_add(1);
            self.device_state.publish_used(self.next_used);
            // Return the index that we just published.
            submit
        }
    }

    #[repr(transparent)]
    pub struct DriverAddr(pub usize);

    pub trait DriverMem {
        // May fail if an address is invalid. Addresses *shouldn't* overlap but they *may*. It is
        // important to remember that this is inherently shared memory that could be being
        // concurrently modified by another thread.
        // As such it is user responsibility to read to local, perform fences and perform error
        // checking.
        fn translate(&self, addr: DriverAddr, len: u32) -> Option<&'static mut [u8]>;
    }

    pub trait DriverNotify {
        fn notify(&self);
    }

    struct Inner<N> {
        driver_state: ring::Driver,
        state: Mutex<RingState>,
        notify: N,
        feature_event_idx: bool,
        sender: crossbeam_channel::Sender<(u16, u32)>,
        receiver: crossbeam_channel::Receiver<(u16, u32)>,
    }

    impl<N: DriverNotify> Inner<N> {
        fn take_avail(&self) -> Option<u16> {
            let mut state = self.state.lock();
            let ret = self.driver_state.get_avail(state.next);
            if ret.is_some() {
                // TODO: worry about events
                state.next = state.next.wrapping_add(1);
            }
            drop(state);
            self.drain_channel();
            ret
        }

        fn return_chain_internal<T: DerefMut<Target = RingState>>(
            &self,
            state: &mut T,
            desc: u16,
            written: u32,
        ) {
            let submitted = state.return_chain(desc, written);
            // Must ensure the read of flags or used_event occurs *after* we have returned the chain
            // and published the index. We also need to ensure that in the event we do send an
            // interrupt that any state and idx updates have been written, so therefore this becomes
            // acquire and release.
            atomic::fence(atomic::Ordering::AcqRel);
            if self.driver_state.needs_interrupt(self.feature_event_idx, submitted) {
                self.notify.notify();
            }
        }

        fn return_chain(&self, desc: u16, written: u32) {
            // TODO: handle case where it is locked
            if let Some(mut guard) = self.state.try_lock() {
                self.return_chain_internal(&mut guard, desc, written);
            } else {
                self.sender
                    .try_send((desc, written))
                    .expect("Sending on unbounded channel should not fail");
            }
            // Resolve any races and drain the channel.
            self.drain_channel();
        }

        fn drain_channel(&self) {
            while !self.receiver.is_empty() {
                if let Some(mut guard) = self.state.try_lock() {
                    while let Ok((desc, written)) = self.receiver.try_recv() {
                        self.return_chain_internal(&mut guard, desc, written);
                    }
                } else {
                    return;
                }
            }
        }
    }

    pub struct Queue<N>(std::sync::Arc<Inner<N>>);

    impl<N> Queue<N> {
        pub fn new(
            desc: &'static [u8],
            avail: &'static [u8],
            used: &'static mut [u8],
            notify: N,
        ) -> Result<Queue<N>, Error> {
            // TODO: errors
            let driver_state = ring::Driver::new(desc, avail).unwrap();
            let device_state = ring::Device::new(used).unwrap();
            if driver_state.count() != device_state.count() {
                panic!("bad");
            }
            let queue_state = RingState { device_state, next: 0, next_used: 0 };
            let (sender, receiver) = crossbeam_channel::unbounded();
            let queue = Inner {
                driver_state,
                state: Mutex::new(queue_state),
                notify,
                feature_event_idx: false,
                sender,
                receiver,
            };
            Ok(Queue(std::sync::Arc::new(queue)))
        }
    }
    impl<N> Clone for Queue<N> {
        fn clone(&self) -> Self {
            Queue(self.0.clone())
        }
    }

    impl<N: DriverNotify> Queue<N> {
        pub fn next_desc(&self) -> Option<DescChain<N>> {
            if let Some(desc_index) = self.0.take_avail() {
                Some(DescChain { queue: self.clone(), first_desc: desc_index, written: 0 })
            } else {
                None
            }
        }
    }

    // This does not implement Deref since technically writable descriptors should
    // *not* be read from, and so a blanket Deref actually makes no sense.
    pub enum Desc<'a> {
        Readable(&'a [u8]),
        Writable(&'a mut [u8]),
    }

    impl<'a> Desc<'a> {
        pub fn new<M: DriverMem>(desc: &'static ring::Desc, mem: &M) -> Result<Desc<'a>, Error> {
            let (addr, len) = desc.data();
            let slice = mem.translate(DriverAddr(addr as usize), len).expect("Memory not in host");
            Ok(if desc.write_only() {
                Desc::Writable(slice)
            } else {
                Desc::Readable(slice)
            })
        }
        pub fn len(&self) -> usize {
            match self {
                Desc::Readable(mem) => mem.len(),
                Desc::Writable(mem) => mem.len(),
            }
        }
    }

    pub struct DescChainIter<'a, N: DriverNotify, M> {
        queue: Queue<N>,
        desc: Option<u16>,
        mem: &'a M,
        chain: &'a mut DescChain<N>,
    }

    impl<'a, N: DriverNotify, M: DriverMem> Iterator for DescChainIter<'a, N, M> {
        type Item = Result<Desc<'a>, Error>;

        fn next(&mut self) -> Option<Self::Item> {
            if let Some(ret) = self.desc {
                let desc = self.queue.0.driver_state.get_desc(ret);
                self.desc = desc.clone().and_then(|d| d.next());
                desc.map(|d| Desc::new(d, self.mem))
            } else {
                None
            }
        }
    }

    impl<'a, N: DriverNotify, M: DriverMem> DescChainIter<'a, N, M> {
        pub fn get_chain(&mut self) -> &mut DescChain<N> {
            self.chain
        }
        /// # Safety
        ///
        /// `get_chain` must only be used from one of the cloner or clonee, otherwise there will
        /// be a memory race.
        pub unsafe fn clone(&self) -> DescChainIter<'a, N, M> {
            DescChainIter {queue: self.queue.clone(), desc: self.desc, mem: self.mem, chain:
                unsafe{&mut*(self.chain as *const DescChain<N> as *mut DescChain<N>)}
            }
        }
    }

    pub struct DescChain<N: DriverNotify> {
        queue: Queue<N>,
        first_desc: u16,
        written: u32,
    }

    impl<N: DriverNotify> DescChain<N> {
        pub fn iter<'a, M: DriverMem>(&'a mut self, mem: &'a M) -> DescChainIter<'a, N, M> {
            DescChainIter {
                queue: self.queue.clone(),
                desc: Some(self.first_desc),
                chain: self,
                mem,
            }
        }
        pub fn set_written(&mut self, written: u32) {
            self.written = written;
        }
    }

    impl<N: DriverNotify> Drop for DescChain<N> {
        fn drop(&mut self) {
            self.queue.0.return_chain(self.first_desc, self.written);
        }
    }

}

pub use crate::queue::*;

mod desc_util {
    use {
        crate::queue::{Desc, DescChain, DescChainIter, DriverMem, DriverNotify, Queue},
        futures::{task::AtomicWaker, Stream, StreamExt},
        std::{
            ops::{Deref, DerefMut},
            pin::Pin,
            task::{Context, Poll},
        },
        failure::{Error, format_err},
        zerocopy,
    };

    pub struct DescChainStream<N> {
        queue: Queue<N>,
        task: std::sync::Arc<AtomicWaker>,
    }

    impl<N> DescChainStream<N> {
        // Creates a stream for descriptor chains.
        // You are responsible for signaling the waker.
        pub fn new(queue: Queue<N>) -> DescChainStream<N> {
            DescChainStream { queue, task: std::sync::Arc::new(AtomicWaker::new()) }
        }

        pub fn waker(&self) -> std::sync::Arc<AtomicWaker> {
            self.task.clone()
        }
    }

    impl<N: DriverNotify> Stream for DescChainStream<N> {
        type Item = DescChain<N>;

        fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            if let Some(desc) = self.queue.next_desc() {
                return Poll::Ready(Some(desc));
            }
            self.task.register(cx.waker());
            match self.queue.next_desc() {
                Some(desc) => Poll::Ready(Some(desc)),
                None => Poll::Pending,
            }
        }
    }

    pub struct DescChainBytes<'a, N: DriverNotify, M: DriverMem> {
        iter: DescChainIter<'a, N, M>,
        state: Option<Desc<'a>>,
        written: u32,
    }

    pub enum ReadView<'a, T> {
        ZeroCopy(zerocopy::LayoutVerified<&'a [u8], T>),
        Copied(T),
    }

    impl<'a, N: DriverNotify, M: DriverMem> Drop for DescChainBytes<'a, N, M> {
        fn drop(&mut self) {
            self.iter.get_chain().set_written(self.written);
        }
    }

    impl<'a, N: DriverNotify, M: DriverMem> DescChainBytes<'a, N, M> {
        pub fn eof(&self) -> bool {
            self.state.is_none()
        }
        pub fn new(mut iter: DescChainIter<'a, N, M>) -> Result<DescChainBytes<'a, N, M>, Error> {
            iter.next().transpose().map(|state| DescChainBytes { iter, state, written: 0})
        }
        fn next_state(&mut self) -> Result<(), Error> {
            self.state = self.iter.next().transpose()?;
            Ok(())
        }
        pub fn next_readable<T: zerocopy::FromBytes>(&mut self) -> Result<ReadView<'a, T>, Error> {
            match self.state.take() {
                None => Err(format_err!("none")),
                Some(Desc::Readable(slice)) => {
                    // Try and take it out
                    let (header, data) = zerocopy::LayoutVerified::new_from_prefix(slice).unwrap();
                    if data.len() == 0 {
                        self.next_state()?;
                    } else {
                        self.state = Some(Desc::Readable(data));
                    }
                    Ok(ReadView::ZeroCopy(header))
                },
                Some(Desc::Writable(slice)) => Err(format_err!("writable")),
            }
        }
        pub fn next_writable<T: zerocopy::AsBytes>(&mut self) -> Result<zerocopy::LayoutVerified<&'a mut [u8], T>, Error> {
            match self.state.take() {
                None => Err(format_err!("none")),
                Some(Desc::Readable(slice)) => Err(format_err!("readable")),
                Some(Desc::Writable(slice)) => {
                    let (header, data) = zerocopy::LayoutVerified::new_from_prefix(slice).unwrap();
                    if data.len() == 0 {
                        self.next_state()?;
                    } else {
                        self.state = Some(Desc::Writable(data));
                    }
                    Ok(header)
                },
            }
        }
        pub fn next_written<T: zerocopy::AsBytes>(&mut self) -> Result<zerocopy::LayoutVerified<&'a mut [u8], T>, Error> {
            let layout = self.next_writable()?;
            self.mark_written(std::mem::size_of::<T>() as u32);
            Ok(layout)
        }
        pub fn readable_remaining(&self) -> usize {
            // TODO: no unwrap
            if let Some(Desc::Readable(slice)) = self.state {
                // This use of clone is safe as we will not call get_chain on on the cloned iterator
                // and so will not use the duplicate mutable reference.
                let rest: usize = unsafe{self.iter.clone()}.map(|x| x.unwrap()).take_while(|x| if let Desc::Readable(_) = x { true } else { false })
                    .map(|x| x.len())
                    .sum();
                rest + slice.len()
            } else {
                0
            }
        }
        pub fn skip_till_readable(&mut self) -> Result<(), ()> {
            unimplemented!()
        }
        pub fn next_bytes_readable(&mut self) -> Result<&'a[u8], Error> {
            self.next_bytes_readable_n(std::usize::MAX)
        }
        pub fn next_bytes_writable(&mut self) -> Result<&'a mut [u8], Error> {
            self.next_bytes_writable_n(std::usize::MAX)
        }
        pub fn next_bytes_readable_n(&mut self, limit: usize) -> Result<&'a[u8], Error> {
            match self.state.take() {
                None => Err(format_err!("none")),
                Some(Desc::Readable(slice)) => {
                    if slice.len() > limit {
                        let (ret, remain) = slice.split_at(limit);
                        self.state = Some(Desc::Readable(remain));
                        Ok(ret)
                    } else {
                        let ret = slice;
                        self.next_state()?;
                        Ok(ret)
                    }
                },
                Some(Desc::Writable(slice)) => Err(format_err!("writable")),
            }
        }
        pub fn next_bytes_writable_n(&mut self, limit: usize) -> Result<&'a mut [u8], Error> {
            match self.state.take() {
                None => Err(format_err!("none")),
                Some(Desc::Writable(slice)) => {
                    if slice.len() > limit {
                        let (ret, remain) = slice.split_at_mut(limit);
                        self.state = Some(Desc::Writable(remain));
                        Ok(ret)
                    } else {
                        let ret = slice;
                        self.next_state()?;
                        Ok(ret)
                    }
                },
                Some(Desc::Readable(slice)) => Err(format_err!("readable")),
            }
        }
        pub fn mark_written(&mut self, written: u32) {
            self.written = self.written + written as u32;
        }
    }
    impl<'a, N: DriverNotify, M: DriverMem> std::io::Read for DescChainBytes<'a, N, M> {
        fn read(&mut self, mut buf: &mut [u8]) -> std::io::Result<usize> {
            match self.next_bytes_readable_n(buf.len()) {
                Ok(slice) => {
                    &mut buf[0..slice.len()].copy_from_slice(slice);
                    Ok(slice.len())
                },
                Err(_) => Ok(0),
            }
        }
    }
    impl<'a, N: DriverNotify, M: DriverMem> std::io::Write for DescChainBytes<'a, N, M> {
        fn write(&mut self, mut buf: &[u8]) -> std::io::Result<usize> {
            match self.next_bytes_writable_n(buf.len()) {
                Ok(slice) => {
                    slice.copy_from_slice(&buf[0..slice.len()]);
                    self.mark_written(slice.len() as u32);
                    Ok(slice.len())
                },
                Err(_) => Ok(0),
            }
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }

}

pub use desc_util::*;

use futures::{task::AtomicWaker, Stream, StreamExt};
use std::ops::Deref;
use std::pin::Pin;
use std::sync::atomic;
use std::task::{Context, Poll};

struct BufferedNotifyInner<N> {
    notify: N,
    was_notified: atomic::AtomicBool,
}

pub struct BufferedNotify<N>(std::sync::Arc<BufferedNotifyInner<N>>);

impl<N> Clone for BufferedNotify<N> {
    fn clone(&self) -> BufferedNotify<N> {
        BufferedNotify(self.0.clone())
    }
}

impl<N> DriverNotify for BufferedNotify<N> {
    fn notify(&self) {
        self.0.was_notified.store(true, atomic::Ordering::Relaxed);
    }
}

impl<N: DriverNotify> BufferedNotify<N> {
    pub fn flush(&self) {
        if self.0.was_notified.swap(false, atomic::Ordering::Relaxed) {
            self.0.notify.notify()
        } else {
        }
    }
}

impl<N> BufferedNotify<N> {
    pub fn new(notify: N) -> BufferedNotify<N> {
        BufferedNotify(std::sync::Arc::new(BufferedNotifyInner{notify, was_notified: atomic::AtomicBool::new(false)}))
    }
}
