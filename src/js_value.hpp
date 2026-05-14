#pragma once
/*
 * C++ RAII wrapper around JsValue for generated blue output.
 * Owning values dispose heap strings/objects on destruction; borrowed
 * values (e.g. handles into static builtin modules) are not freed.
 */
#include "js_value.h"
#include <utility>

class JsOwned {
    JsValue v_;
    bool borrowed_;

public:
    JsOwned() : v_(js_undefined()), borrowed_(false) {}
    JsOwned(JsValue v) : v_(v), borrowed_(false) {}

    static JsOwned from_value(JsValue v) {
        JsOwned o;
        o.v_ = v;
        o.borrowed_ = false;
        return o;
    }

    static JsOwned from_borrowed(JsValue v) {
        JsOwned o;
        o.v_ = v;
        o.borrowed_ = true;
        return o;
    }

    JsOwned clone() const { return from_value(js_clone_value(v_)); }

    ~JsOwned() {
        if (!borrowed_)
            js_dispose_value(&v_);
    }

    JsOwned(JsOwned&& o) noexcept : v_(o.v_), borrowed_(o.borrowed_) {
        o.v_ = js_undefined();
        o.borrowed_ = false;
    }

    JsOwned& operator=(JsOwned&& o) noexcept {
        if (this != &o) {
            if (!borrowed_)
                js_dispose_value(&v_);
            v_ = o.v_;
            borrowed_ = o.borrowed_;
            o.v_ = js_undefined();
            o.borrowed_ = false;
        }
        return *this;
    }

    JsOwned& operator=(JsValue v) {
        if (!borrowed_ && v_.tag != JS_UNDEFINED && (v_.tag != v.tag || (v_.tag == JS_STRING && v_.as.string != v.as.string) || (v_.tag == JS_OBJECT && v_.as.obj != v.as.obj) || (v_.tag == JS_BYTES && v_.as.bytes != v.as.bytes))) {
            js_dispose_value(&v_);
        }
        v_ = v;
        borrowed_ = false;
        return *this;
    }

    JsValue release() {
        JsValue v = v_;
        v_ = js_undefined();
        borrowed_ = true;
        return v;
    }

    JsOwned(const JsOwned&) = delete;
    JsOwned& operator=(const JsOwned&) = delete;

    JsValue borrow() const { return v_; }

    JsValue* ptr() { return &v_; }

    const JsValue* ptr() const { return &v_; }
};
