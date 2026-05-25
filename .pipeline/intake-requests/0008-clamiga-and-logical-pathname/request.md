---
id: 0008-clamiga-and-logical-pathname
type: bug
status: done
title: clamiga and logical pathname
---

# clamiga and logical pathname

So clamiga has LOGICAL-PATHNAME interned as a symbol and recognizes it as a type (typep returns nil, builtins_type.c:233), but there's no CLASS object for it. defmethod specializers need a class —
  find-class 'logical-pathname fails — so (defmethod emacs-inspect ((x logical-pathname)) …) blows up at load.

  clamiga-side fix: define LOGICAL-PATHNAME as a class, a subclass of PATHNAME (that's the standard CL hierarchy — logical-pathname is a subtype of pathname). PATHNAME is already a STANDARD-CLASS in
  clamiga, so this mirrors it. Nothing needs to instantiate it; the class just has to exist so the method specializer resolves. That unblocks slynk-fancy-inspector.

  Lower priority (used only in that method's body, which won't run since clamiga produces no logical pathnames, but they're standard CL functions worth having): translate-logical-pathname and
  logical-pathname-translations are also unbound. (ASDF defines its own stubs in lib/asdf.lisp, but in ASDF's package, not CL.)
