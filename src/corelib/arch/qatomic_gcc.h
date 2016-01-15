/****************************************************************************
**
** Copyright (C) 2011 Thiago Macieira <thiago@kde.org>
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QATOMIC_GCC_H
#define QATOMIC_GCC_H

#include <QtCore/qgenericatomic.h>

QT_BEGIN_NAMESPACE

#if 0
// silence syncqt warnings
QT_END_NAMESPACE
#pragma qt_sync_skip_header_check
#pragma qt_sync_stop_processing
#endif

#define Q_ATOMIC_INT_REFERENCE_COUNTING_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_INT_TEST_AND_SET_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_INT_FETCH_AND_STORE_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_INT_FETCH_AND_ADD_IS_SOMETIMES_NATIVE

#define Q_ATOMIC_INT32_IS_SUPPORTED
#define Q_ATOMIC_INT32_REFERENCE_COUNTING_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_INT32_TEST_AND_SET_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_INT32_FETCH_AND_STORE_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_INT32_FETCH_AND_ADD_IS_SOMETIMES_NATIVE

#define Q_ATOMIC_POINTER_REFERENCE_COUNTING_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_POINTER_TEST_AND_SET_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_POINTER_FETCH_AND_STORE_IS_SOMETIMES_NATIVE
#define Q_ATOMIC_POINTER_FETCH_AND_ADD_IS_SOMETIMES_NATIVE

#if QT_POINTER_SIZE == 8
#  define Q_ATOMIC_INT64_IS_SUPPORTED
#  define Q_ATOMIC_INT64_REFERENCE_COUNTING_IS_SOMETIMES_NATIVE
#  define Q_ATOMIC_INT64_TEST_AND_SET_IS_SOMETIMES_NATIVE
#  define Q_ATOMIC_INT64_FETCH_AND_STORE_IS_SOMETIMES_NATIVE
#  define Q_ATOMIC_INT64_FETCH_AND_ADD_IS_SOMETIMES_NATIVE
template<> struct QAtomicOpsSupport<8> { enum { IsSupported = 1 }; };
#endif

template <typename X> struct QAtomicOps: QGenericAtomicOps<QAtomicOps<X> >
{
    // The GCC intrinsics all have fully-ordered memory semantics, so we define
    // only the xxxRelaxed functions. The exception is __sync_lock_and_test,
    // which has acquire semantics, so we need to define the Release and
    // Ordered versions too.

    typedef X Type;

#ifndef __ia64__
    template <typename T>
    static T loadAcquire(const T &_q_value) Q_DECL_NOTHROW
    {
        T tmp = _q_value;
        __sync_synchronize();
        return tmp;
    }

    template <typename T>
    static void storeRelease(T &_q_value, T newValue) Q_DECL_NOTHROW
    {
        __sync_synchronize();
        _q_value = newValue;
    }
#endif

    static Q_DECL_CONSTEXPR bool isTestAndSetNative() Q_DECL_NOTHROW { return false; }
    static Q_DECL_CONSTEXPR bool isTestAndSetWaitFree() Q_DECL_NOTHROW { return false; }
    template <typename T>
    static bool testAndSetRelaxed(T &_q_value, T expectedValue, T newValue) Q_DECL_NOTHROW
    {
        return __sync_bool_compare_and_swap(&_q_value, expectedValue, newValue);
    }

    template <typename T>
    static bool testAndSetRelaxed(T &_q_value, T expectedValue, T newValue, T *currentValue) Q_DECL_NOTHROW
    {
        bool tmp = __sync_bool_compare_and_swap(&_q_value, expectedValue, newValue);
        if (tmp)
            *currentValue = expectedValue;
        else
            *currentValue = _q_value;
        return tmp;
    }

    template <typename T>
    static T fetchAndStoreRelaxed(T &_q_value, T newValue) Q_DECL_NOTHROW
    {
        return __sync_lock_test_and_set(&_q_value, newValue);
    }

    template <typename T>
    static T fetchAndStoreRelease(T &_q_value, T newValue) Q_DECL_NOTHROW
    {
        __sync_synchronize();
        return __sync_lock_test_and_set(&_q_value, newValue);
    }

    template <typename T>
    static T fetchAndStoreOrdered(T &_q_value, T newValue) Q_DECL_NOTHROW
    {
        return fetchAndStoreRelease(_q_value, newValue);
    }

    template <typename T> static
    T fetchAndAddRelaxed(T &_q_value, typename QAtomicAdditiveType<T>::AdditiveT valueToAdd) Q_DECL_NOTHROW
    {
        return __sync_fetch_and_add(&_q_value, valueToAdd * QAtomicAdditiveType<T>::AddScale);
    }
};

QT_END_NAMESPACE
#endif // QATOMIC_GCC_H
