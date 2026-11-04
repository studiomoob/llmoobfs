----------------------------- MODULE IndexLoad -----------------------------
(***************************************************************************)
(* SPDX-License-Identifier: GPL-2.0-only                                   *)
(*                                                                         *)
(* A model of the on-demand build of one domain of the MoobFS index        *)
(* (docs/spec-query-v2.md section 9) against the update hooks that run     *)
(* while it is built. Security roadmap S21.                                *)
(*                                                                         *)
(* Subject: layer/index.c index_load(), its loader registration on         *)
(* ix->loaders, the reading of a file with no lock held, and the retry     *)
(* when an update hook marked the loader dirty (index_touched()); the      *)
(* scan that calls it (scan_run(), scan_child(), scan_file()); the start   *)
(* and the end of a build attempt (domain_ensure_dom()); and the hooks of  *)
(* layer/main.c that change the index: moobl_index_set(),                  *)
(* moobl_index_remove(), moobl_index_perm_changed(),                       *)
(* moobl_index_renamed() (file branch, with index_adopt()),                *)
(* moobl_index_linked() and moobl_index_drop_inode().                      *)
(*                                                                         *)
(* One domain, regular files directly inside the domain directory.         *)
(* formal/README.md lists what is abstracted and what is left out.         *)
(*                                                                         *)
(* Granularity. Every action that takes the index write lock               *)
(* (ix_write_lock()) is one atomic step, which is what the lock gives.     *)
(* Each reading of the lower filesystem that the code makes with no lock   *)
(* of ours held is its own step: the dentry checks, target_ok(), and each  *)
(* attribute of attrs_read(). A lower change made through the layer        *)
(* (vfs_setxattr(), notify_change(), vfs_rename(), vfs_link(),             *)
(* vfs_unlink()) is one step, and its hook another, later one, as in       *)
(* main.c where the hook runs after the lower call returned.               *)
(*                                                                         *)
(* Writers. The VFS serializes setxattr, setattr, rename, link and unlink  *)
(* of one file on that file's i_rwsem, and the hooks run under it          *)
(* (main.c: moobl_xattr_set(), moobl_setattr(), moobl_rename(),            *)
(* moobl_link(), moobl_unlink()). So one writer process per file, doing    *)
(* one operation after the other, covers every order of operations on      *)
(* that file. The scan takes no upper inode lock and interleaves freely.   *)
(* A hook's placement (place_dentry(), place_and_lock()) is computed with  *)
(* no lock held, but it reads only this file's lower inode, which only     *)
(* the same writer changes, so it is taken inside the hook's locked step.  *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets

CONSTANTS
    Files,      \* lower inodes in the domain directory; also the writer process ids
    NAttrs,     \* typed attribute names 1..NAttrs (user.moob.*), value 0 = absent, 1 = present
    NNames,     \* names a file may have in the directory: 1..NNames (per file, disjoint)
    MaxLinks,   \* how many hard links a writer may add: dentry slots 1..MaxLinks+1
    MaxOps,     \* lower operations per writer (the bound of the configuration)
    MaxTries,   \* readings of one file before index_load() gives up (MOOBL_LOAD_TRIES = 16)
    MaxBuilds,  \* build attempts (MOOBL_BUILD_TRIES = 4)
    \* Switches. All TRUE is the code as it is; each FALSE puts back one defect.
    Retry,                  \* index_load() reads again when ld.dirty is set (the S21 control)
    ClassifyAfterRegister,  \* target_ok() inside index_load(), after registration (journal 0152)
    CheckName,              \* name_is() check of the scanned name (R2-2, journal 0156)
    ListMayMiss             \* readdir may miss a name renamed or linked during the listing

ASSUME NAttrs \in Nat \ {0} /\ NNames \in Nat \ {0} /\ MaxLinks \in Nat
ASSUME MaxOps \in Nat /\ MaxTries \in Nat \ {0} /\ MaxBuilds \in Nat \ {0}
ASSUME {Retry, ClassifyAfterRegister, CheckName, ListMayMiss} \subseteq BOOLEAN

B == "builder"                 \* the task that runs domain_ensure_dom()
Procs == {B} \cup Files        \* every task that may own a struct moobl_loader
A == 1..NAttrs
Names == 1..NNames
D == 1..(MaxLinks + 1)         \* dentry slots of one file; slot 1 is its initial name
NoName == 0
Zero == [i \in A |-> 0]
NoRec == [a |-> Zero, het |-> FALSE, path |-> NoName]

VARIABLES
    low,        \* the lower filesystem: low[f] = [a, perm, dn, next]
                \*   a[i]   typed attribute i (what vfs_getxattr() returns)
                \*   perm   mode/owner/ACL pass reach_ok() against the domain directory
                \*   dn[d]  name of dentry d, NoName once unhashed or not yet made
                \*   next   first dentry slot never used (vfs_link() makes a new dentry)
    dirLock,    \* the lower directory's i_rwsem held for writing across a layer rename:
                \* moobl_rename() takes lock_rename() before vfs_rename() and releases it
                \* (unlock_rename()) only after moobl_index_renamed() and index_adopt()
                \* returned. "none", or the file whose writer holds it.
    dstate,    \* struct moobl_domain .state: "NONE", "BUILDING", "BUILT"
    rec,        \* ix->inodes: rec[f] = [a, het, path]; NoRec when there is none
    reg, dirty, \* struct moobl_loader on ix->loaders, per owning task
    changed,    \* dentries of f renamed or linked since the attempt began (readdir may miss them)
    \* builder (domain_ensure_dom() -> scan_run() -> scan_file() -> index_load())
    bpc, bAttempt, bTodo, bFile, bNames, bD, bPath, bHet, bBuf, bIdx, bTries,
    \* writer of file f (main.c hook -> index.c hook -> index_adopt() -> index_load())
    wpc, wOps, wI, wD, wPath, wHet, wBuf, wIdx, wAfter

lowerVars == <<low, dirLock>>
ixVars == <<dstate, rec, reg, dirty, changed>>
bVars == <<bpc, bAttempt, bTodo, bFile, bNames, bD, bPath, bHet, bBuf, bIdx, bTries>>
wVars == <<wpc, wOps, wI, wD, wPath, wHet, wBuf, wIdx, wAfter>>
vars == <<lowerVars, ixVars, bVars, wVars>>

LiveD(f) == {d \in D : low[f].dn[d] # NoName}
NamesOf(f) == {low[f].dn[d] : d \in LiveD(f)}
NLink(f) == Cardinality(LiveD(f))
\* target_ok(): a regular file with i_nlink > 1 fails, else reach_ok() (here: perm).
Ok(f) == low[f].perm /\ NLink(f) <= 1
Present(r) == \E i \in A : r.a[i] = 1
Live == dstate # "NONE"                         \* domain_live()
Whole(f) == Present(rec[f]) /\ dstate = "BUILT" \* rec && rec_whole()

\* index_touched(): every loader of this inode is marked dirty. Caller holds the lock.
Touch(f) == [p \in Procs |-> IF reg[p] /\ (p = B => bFile = f) /\ (p # B => p = f)
                              THEN TRUE ELSE dirty[p]]

\* index_replace(): the record becomes exactly what was read (none if nothing was).
Replace(r, buf, het, path) == IF \E i \in A : buf[i] = 1
                              THEN [a |-> buf, het |-> het, path |-> path]
                              ELSE NoRec
\* index_put() + record_dentry_path(): one attribute, the record moved to the
\* placement given (rec_set_partition()), the name of the dentry written through.
Put(r, i, het, path) == [a |-> [r.a EXCEPT ![i] = 1], het |-> het, path |-> path]
\* entry_free() + inode_rec_put_if_empty()
RemoveEntry(r, i) == IF \E j \in A \ {i} : r.a[j] = 1
                     THEN [r EXCEPT !.a[i] = 0] ELSE NoRec
\* rec_set_partition() on an existing record
SetHet(r, het) == IF Present(r) THEN [r EXCEPT !.het = het] ELSE r

-----------------------------------------------------------------------------
Init ==
    /\ low = [f \in Files |-> [a |-> [i \in A |-> IF i = 1 THEN 1 ELSE 0],
                                perm |-> TRUE,
                                dn |-> [d \in D |-> IF d = 1 THEN 1 ELSE NoName],
                                next |-> 2]]
    /\ dirLock = "none"
    /\ dstate = "NONE"          \* nothing is indexed at mount (ADR-0012)
    /\ rec = [f \in Files |-> NoRec]
    /\ reg = [p \in Procs |-> FALSE]
    /\ dirty = [p \in Procs |-> FALSE]
    /\ changed = [f \in Files |-> {}]
    /\ bpc = "start" /\ bAttempt = 0 /\ bTodo = {} /\ bFile = CHOOSE f \in Files : TRUE
    /\ bNames = {} /\ bD = 1 /\ bPath = NoName /\ bHet = FALSE /\ bBuf = Zero
    /\ bIdx = 1 /\ bTries = 0
    /\ wpc = [f \in Files |-> "idle"] /\ wOps = [f \in Files |-> 0]
    /\ wI = [f \in Files |-> 1] /\ wD = [f \in Files |-> 1]
    /\ wPath = [f \in Files |-> NoName] /\ wHet = [f \in Files |-> FALSE]
    /\ wBuf = [f \in Files |-> Zero] /\ wIdx = [f \in Files |-> 1]
    /\ wAfter = [f \in Files |-> "idle"]

-----------------------------------------------------------------------------
(* The builder.                                                            *)

\* domain_ensure_dom(): under the lock, state = BUILDING; the scan starts.
BStart ==
    /\ bpc = "start"
    /\ dstate' = "BUILDING"
    /\ changed' = [f \in Files |-> {}]
    /\ bTodo' = Files
    /\ bpc' = "list"
    /\ UNCHANGED <<lowerVars, rec, reg, dirty, bAttempt, bFile, bNames, bD, bPath,
                   bHet, bBuf, bIdx, bTries, wVars>>

\* scan_run(): iterate_dir() returns the names of f present at that moment, in some
\* order. With ListMayMiss, a name whose dentry was renamed or linked since the attempt
\* began may be missed: a rename between two batches can move it into the part already
\* read (iterate_dir() holds the directory lock only for one batch). iterate_dir() takes
\* the lower directory's i_rwsem shared, so it cannot run inside a layer rename.
BList ==
    /\ bpc = "list"
    /\ bTodo # {}
    /\ dirLock = "none"
    /\ \E f \in bTodo : \E S \in SUBSET NamesOf(f) :
          /\ (NamesOf(f) \ S) \subseteq
                (IF ListMayMiss THEN {low[f].dn[d] : d \in changed[f] \cap LiveD(f)} ELSE {})
          /\ bFile' = f /\ bNames' = S /\ bTodo' = bTodo \ {f}
    /\ bpc' = "lookup"
    /\ UNCHANGED <<lowerVars, ixVars, bAttempt, bD, bPath, bHet, bBuf, bIdx, bTries, wVars>>

\* Every file listed: the side-list rounds find nothing (no directory in this model),
\* and under the same lock the domain becomes BUILT (domain_ensure_dom()).
BFinish ==
    /\ bpc = "list"
    /\ bTodo = {}
    /\ dstate' = "BUILT"
    /\ bpc' = "done"
    /\ UNCHANGED <<lowerVars, rec, reg, dirty, changed, bAttempt, bTodo, bFile, bNames,
                   bD, bPath, bHet, bBuf, bIdx, bTries, wVars>>

\* scan_child(): lookup_one_len_unlocked() of a listed name. A negative dentry is
\* skipped. A positive answer may come from the dcache with no lock (d_lookup() under
\* rename_lock), so it is allowed at any time. A negative answer for the old name of a
\* renamed dentry needs lookup_slow(), which takes the directory's i_rwsem shared
\* (d_move() leaves no hashed dentry under the old name), so it waits for the rename
\* and its hook. Without ClassifyAfterRegister, scan_file() computes the file's own
\* homogeneity here, before index_load() registers (the code before journal 0152).
BLookup ==
    /\ bpc = "lookup"
    /\ IF bNames = {}
       THEN /\ bpc' = "list" /\ UNCHANGED <<bNames, bD, bPath, bHet>>
       ELSE \E n \in bNames :
              /\ bNames' = bNames \ {n}
              /\ IF \E d \in LiveD(bFile) : low[bFile].dn[d] = n
                 THEN LET d == CHOOSE d \in LiveD(bFile) : low[bFile].dn[d] = n IN
                      /\ bD' = d /\ bPath' = n
                      /\ bHet' = IF ClassifyAfterRegister THEN bHet ELSE ~Ok(bFile)
                      /\ bpc' = "register"
                 ELSE /\ dirLock = "none"
                      /\ bpc' = "lookup" /\ UNCHANGED <<bD, bPath, bHet>>
    /\ UNCHANGED <<lowerVars, ixVars, bAttempt, bTodo, bFile, bBuf, bIdx, bTries, wVars>>

\* index_load(): list_add(&ld.list, &ix->loaders) under the lock.
BRegister ==
    /\ bpc = "register"
    /\ reg' = [reg EXCEPT ![B] = TRUE] /\ dirty' = [dirty EXCEPT ![B] = FALSE]
    /\ bTries' = 0
    /\ bpc' = "check"
    /\ UNCHANGED <<lowerVars, dstate, rec, changed, bAttempt, bTodo, bFile, bNames, bD,
                   bPath, bHet, bBuf, bIdx, wVars>>

\* index_load() loop head, no lock: d_unhashed(), then name_is(). Either one ends the
\* load with success: the hook that removed or renamed the name updated the index.
\* The unregistration is the lock section after the loop.
BCheck ==
    /\ bpc = "check"
    /\ IF low[bFile].dn[bD] = NoName \/ (CheckName /\ low[bFile].dn[bD] # bPath)
       THEN bpc' = "unregister"
       ELSE bpc' = IF ClassifyAfterRegister THEN "classify" ELSE "read"
    /\ bIdx' = 1
    /\ UNCHANGED <<lowerVars, ixVars, bAttempt, bTodo, bFile, bNames, bD, bPath, bHet,
                   bBuf, bTries, wVars>>

\* index_load(): cur.hetero = !target_ok(li, dom), no lock (journal 0152).
BClassify ==
    /\ bpc = "classify"
    /\ bHet' = ~Ok(bFile)
    /\ bpc' = "read"
    /\ UNCHANGED <<lowerVars, ixVars, bAttempt, bTodo, bFile, bNames, bD, bPath, bBuf,
                   bIdx, bTries, wVars>>

\* attrs_read(): one attribute at a time, no lock (vfs_listxattr(), vfs_getxattr()).
BRead ==
    /\ bpc = "read"
    /\ bBuf' = [bBuf EXCEPT ![bIdx] = low[bFile].a[bIdx]]
    /\ IF bIdx = NAttrs THEN bpc' = "store" /\ bIdx' = bIdx
                        ELSE bpc' = "read" /\ bIdx' = bIdx + 1
    /\ UNCHANGED <<lowerVars, ixVars, bAttempt, bTodo, bFile, bNames, bD, bPath, bHet,
                   bTries, wVars>>

\* index_load(), under the lock: if ld.dirty, clear it and read again (or, after
\* MOOBL_LOAD_TRIES readings, give up with -EAGAIN); else list_del() and
\* index_replace() with the scanned path (index_set_path()).
BStore ==
    /\ bpc = "store"
    /\ IF Retry /\ dirty[B]
       THEN /\ dirty' = [dirty EXCEPT ![B] = FALSE]
            /\ bTries' = bTries + 1
            /\ bpc' = IF bTries + 1 = MaxTries THEN "giveup" ELSE "check"
            /\ UNCHANGED <<rec, reg>>
       ELSE /\ reg' = [reg EXCEPT ![B] = FALSE]
            /\ rec' = [rec EXCEPT ![bFile] = Replace(rec[bFile], bBuf, bHet, bPath)]
            /\ bpc' = "lookup"
            /\ UNCHANGED <<dirty, bTries>>
    /\ UNCHANGED <<lowerVars, dstate, changed, bAttempt, bTodo, bFile, bNames, bD, bPath,
                   bHet, bBuf, bIdx, wVars>>

\* index_load() after the loop, under the lock: list_del(); returns 0 (removed or
\* renamed) or -EAGAIN (every reading overtaken).
BUnregister ==
    /\ bpc \in {"unregister", "giveup"}
    /\ reg' = [reg EXCEPT ![B] = FALSE]
    /\ bpc' = IF bpc = "giveup" THEN "fail" ELSE "lookup"
    /\ UNCHANGED <<lowerVars, dstate, rec, dirty, changed, bAttempt, bTodo, bFile, bNames,
                   bD, bPath, bHet, bBuf, bIdx, bTries, wVars>>

\* domain_ensure_dom() on an error, under the lock: domain_drop() takes every record
\* of the domain out, state = NONE; with -EAGAIN the next attempt starts.
BFail ==
    /\ bpc = "fail"
    /\ rec' = [f \in Files |-> NoRec]
    /\ dstate' = "NONE"
    /\ bAttempt' = bAttempt + 1
    /\ bpc' = IF bAttempt + 1 < MaxBuilds THEN "start" ELSE "done"
    /\ UNCHANGED <<lowerVars, reg, dirty, changed, bTodo, bFile, bNames, bD, bPath, bHet,
                   bBuf, bIdx, bTries, wVars>>

Builder == BStart \/ BList \/ BFinish \/ BLookup \/ BRegister \/ BCheck \/ BClassify
           \/ BRead \/ BStore \/ BUnregister \/ BFail

-----------------------------------------------------------------------------
(* The writers: a lower change, then its hook.                             *)

Begin(f) == wpc[f] = "idle" /\ wOps[f] < MaxOps /\ LiveD(f) # {}
Op(f) == wOps' = [wOps EXCEPT ![f] = wOps[f] + 1]
WGo(f, pc) == wpc' = [wpc EXCEPT ![f] = pc]

\* moobl_xattr_set(): vfs_setxattr() of a typed attribute through dentry d.
WSetLower(f) ==
    /\ Begin(f)
    /\ \E i \in A, d \in LiveD(f) :
          /\ low[f].a[i] = 0
          /\ low' = [low EXCEPT ![f].a[i] = 1]
          /\ wI' = [wI EXCEPT ![f] = i] /\ wD' = [wD EXCEPT ![f] = d]
    /\ Op(f) /\ WGo(f, "set")
    /\ UNCHANGED <<dirLock, ixVars, bVars, wPath, wHet, wBuf, wIdx, wAfter>>

\* moobl_index_set(): i_nlink is not 0 (same writer); place_and_lock();
\* index_touched(); if the domain is not live, nothing (the build will read it);
\* else index_put() and record_dentry_path().
WSetHook(f) ==
    /\ wpc[f] = "set"
    /\ dirty' = Touch(f)
    /\ rec' = IF Live THEN [rec EXCEPT ![f] = Put(rec[f], wI[f], ~Ok(f), low[f].dn[wD[f]])]
                      ELSE rec
    /\ WGo(f, "idle")
    /\ UNCHANGED <<lowerVars, dstate, reg, changed, bVars, wOps, wI, wD, wPath, wHet,
                   wBuf, wIdx, wAfter>>

\* moobl_xattr_set(): vfs_removexattr() of a typed attribute.
WRemoveLower(f) ==
    /\ Begin(f)
    /\ \E i \in A :
          /\ low[f].a[i] = 1
          /\ low' = [low EXCEPT ![f].a[i] = 0]
          /\ wI' = [wI EXCEPT ![f] = i]
    /\ Op(f) /\ WGo(f, "remove")
    /\ UNCHANGED <<dirLock, ixVars, bVars, wD, wPath, wHet, wBuf, wIdx, wAfter>>

\* moobl_index_remove(): index_touched(); the entry goes, the record if emptied.
WRemoveHook(f) ==
    /\ wpc[f] = "remove"
    /\ dirty' = Touch(f)
    /\ rec' = [rec EXCEPT ![f] = IF Present(rec[f]) THEN RemoveEntry(rec[f], wI[f])
                                                      ELSE rec[f]]
    /\ WGo(f, "idle")
    /\ UNCHANGED <<lowerVars, dstate, reg, changed, bVars, wOps, wI, wD, wPath, wHet,
                   wBuf, wIdx, wAfter>>

\* moobl_setattr() / moobl_set_acl(): notify_change() or vfs_set_acl() changes mode,
\* owner, group or access ACL; abstracted as reach_ok() flipping.
WChmodLower(f) ==
    /\ Begin(f)
    /\ low' = [low EXCEPT ![f].perm = ~low[f].perm]
    /\ Op(f) /\ WGo(f, "perm")
    /\ UNCHANGED <<dirLock, ixVars, bVars, wI, wD, wPath, wHet, wBuf, wIdx, wAfter>>

\* moobl_index_perm_changed() (file case): place_and_lock(); index_touched(); a record
\* in a domain that is not live is dropped; else rec_set_partition().
\* Also the second hook of moobl_link() (wpc "linkperm").
WPermHook(f) ==
    /\ wpc[f] \in {"perm", "linkperm"}
    /\ dirty' = Touch(f)
    /\ rec' = [rec EXCEPT ![f] = IF Present(rec[f]) /\ ~Live THEN NoRec
                                 ELSE SetHet(rec[f], ~Ok(f))]
    /\ WGo(f, "idle")
    /\ UNCHANGED <<lowerVars, dstate, reg, changed, bVars, wOps, wI, wD, wPath, wHet,
                   wBuf, wIdx, wAfter>>

\* moobl_rename() of a file within the directory: vfs_rename() gives dentry d a name
\* the file does not have (no rename over another file in this model).
WRenameLower(f) ==
    /\ Begin(f)
    /\ dirLock = "none" /\ dirLock' = f
    /\ \E d \in LiveD(f), n \in Names \ NamesOf(f) :
          /\ low' = [low EXCEPT ![f].dn[d] = n]
          /\ wD' = [wD EXCEPT ![f] = d]
          /\ changed' = [changed EXCEPT ![f] = changed[f] \cup {d}]
    /\ Op(f) /\ WGo(f, "rename")
    /\ UNCHANGED <<dstate, rec, reg, dirty, bVars, wI, wPath, wHet, wBuf, wIdx, wAfter>>

\* moobl_index_renamed(), file branch: place_dentry(); under the lock index_touched();
\* not live: the record goes; a whole record (domain BUILT): record_dentry_path(where)
\* and rec_set_partition(); otherwise (no record, or the domain is being built)
\* index_adopt() reads the file whole after the lock is dropped.
WRenameHook(f) ==
    /\ wpc[f] = "rename"
    /\ dirty' = Touch(f)
    /\ LET n == low[f].dn[wD[f]] IN
       IF ~Live
       THEN /\ rec' = [rec EXCEPT ![f] = NoRec]
            /\ dirLock' = "none"   \* unlock_rename()
            /\ WGo(f, "idle") /\ UNCHANGED <<wPath, wHet, wAfter>>
       ELSE IF Whole(f)
       THEN /\ rec' = [rec EXCEPT ![f] = [rec[f] EXCEPT !.path = n, !.het = ~Ok(f)]]
            /\ dirLock' = "none"   \* unlock_rename()
            /\ WGo(f, "idle") /\ UNCHANGED <<wPath, wHet, wAfter>>
       ELSE /\ UNCHANGED <<rec, dirLock>>   \* index_adopt() runs before unlock_rename()
            /\ wPath' = [wPath EXCEPT ![f] = n] /\ wHet' = [wHet EXCEPT ![f] = ~Ok(f)]
            /\ wAfter' = [wAfter EXCEPT ![f] = "idle"]
            /\ WGo(f, "adopt")
    /\ UNCHANGED <<low, dstate, reg, changed, bVars, wOps, wI, wD, wBuf, wIdx>>

\* moobl_link(): vfs_link() makes a new dentry with a name the file does not have.
WLinkLower(f) ==
    /\ Begin(f)
    /\ low[f].next <= MaxLinks + 1
    /\ dirLock = "none"   \* moobl_lock_parent() around vfs_link() only
    /\ \E n \in Names \ NamesOf(f) :
          /\ low' = [low EXCEPT ![f].dn[low[f].next] = n, ![f].next = low[f].next + 1]
          /\ wD' = [wD EXCEPT ![f] = low[f].next]
          /\ changed' = [changed EXCEPT ![f] = changed[f] \cup {low[f].next}]
    /\ Op(f) /\ WGo(f, "link")
    /\ UNCHANGED <<dirLock, dstate, rec, reg, dirty, bVars, wI, wPath, wHet, wBuf, wIdx, wAfter>>

\* moobl_index_linked(): place_dentry(); under the lock index_touched(); adopt if the
\* domain is live and the record is not whole; then moobl_index_perm_changed() on the
\* old name (nlink > 1: heterogeneous).
WLinkHook(f) ==
    /\ wpc[f] = "link"
    /\ dirty' = Touch(f)
    /\ IF Live /\ ~Whole(f)
       THEN /\ wPath' = [wPath EXCEPT ![f] = low[f].dn[wD[f]]]
            /\ wHet' = [wHet EXCEPT ![f] = ~Ok(f)]
            /\ wAfter' = [wAfter EXCEPT ![f] = "linkperm"]
            /\ WGo(f, "adopt")
       ELSE /\ WGo(f, "linkperm") /\ UNCHANGED <<wPath, wHet, wAfter>>
    /\ UNCHANGED <<lowerVars, dstate, rec, reg, changed, bVars, wOps, wI, wD, wBuf, wIdx>>

\* moobl_unlink(): vfs_unlink() unhashes dentry d. Only when the last name goes does
\* main.c call moobl_index_drop_inode(); with other names left there is no hook.
WUnlinkLower(f) ==
    /\ Begin(f)
    /\ dirLock = "none"   \* moobl_lock_parent() around vfs_unlink() only
    /\ \E d \in LiveD(f) :
          /\ low' = [low EXCEPT ![f].dn[d] = NoName]
          /\ WGo(f, IF NLink(f) = 1 THEN "drop" ELSE "idle")
    /\ Op(f)
    /\ UNCHANGED <<dirLock, ixVars, bVars, wI, wD, wPath, wHet, wBuf, wIdx, wAfter>>

\* moobl_index_drop_inode(): index_touched(); the record goes.
WDropHook(f) ==
    /\ wpc[f] = "drop"
    /\ dirty' = Touch(f)
    /\ rec' = [rec EXCEPT ![f] = NoRec]
    /\ WGo(f, "idle")
    /\ UNCHANGED <<lowerVars, dstate, reg, changed, bVars, wOps, wI, wD, wPath, wHet,
                   wBuf, wIdx, wAfter>>

\* index_adopt() -> index_load(sb, lower, NULL, pl, NULL, NULL, where): the placement
\* comes from the hook (dom NULL: no target_ok() inside), the name from where.
\* Registration under the lock. The loop-head checks are left out: the writer holds
\* the file's i_rwsem, so the dentry can be neither unlinked nor renamed meanwhile.
WAdoptRegister(f) ==
    /\ wpc[f] = "adopt"
    /\ reg' = [reg EXCEPT ![f] = TRUE] /\ dirty' = [dirty EXCEPT ![f] = FALSE]
    /\ wIdx' = [wIdx EXCEPT ![f] = 1]
    /\ WGo(f, "adread")
    /\ UNCHANGED <<lowerVars, dstate, rec, changed, bVars, wOps, wI, wD, wPath, wHet,
                   wBuf, wAfter>>

WAdoptRead(f) ==
    /\ wpc[f] = "adread"
    /\ wBuf' = [wBuf EXCEPT ![f][wIdx[f]] = low[f].a[wIdx[f]]]
    /\ IF wIdx[f] = NAttrs THEN WGo(f, "adstore") /\ UNCHANGED wIdx
                           ELSE WGo(f, "adread") /\ wIdx' = [wIdx EXCEPT ![f] = wIdx[f] + 1]
    /\ UNCHANGED <<lowerVars, ixVars, bVars, wOps, wI, wD, wPath, wHet, wAfter>>

\* Under the lock: ld.dirty can only be set by a hook of this same file, which the
\* i_rwsem excludes, so the retry is never taken here; list_del(), index_replace(),
\* record_dentry_path(where).
WAdoptStore(f) ==
    /\ wpc[f] = "adstore"
    /\ IF Retry /\ dirty[f]
       THEN /\ dirty' = [dirty EXCEPT ![f] = FALSE] /\ wIdx' = [wIdx EXCEPT ![f] = 1]
            /\ WGo(f, "adread") /\ UNCHANGED <<rec, reg, dirLock>>
       ELSE /\ reg' = [reg EXCEPT ![f] = FALSE]
            /\ rec' = [rec EXCEPT ![f] = Replace(rec[f], wBuf[f], wHet[f], wPath[f])]
            \* back in moobl_rename(): unlock_rename(); a link's adoption holds no lock
            /\ dirLock' = IF dirLock = f THEN "none" ELSE dirLock
            /\ WGo(f, wAfter[f]) /\ UNCHANGED <<dirty, wIdx>>
    /\ UNCHANGED <<low, dstate, changed, bVars, wOps, wI, wD, wPath, wHet, wBuf,
                   wAfter>>

Writer(f) == WSetLower(f) \/ WSetHook(f) \/ WRemoveLower(f) \/ WRemoveHook(f)
             \/ WChmodLower(f) \/ WPermHook(f) \/ WRenameLower(f) \/ WRenameHook(f)
             \/ WLinkLower(f) \/ WLinkHook(f) \/ WUnlinkLower(f) \/ WDropHook(f)
             \/ WAdoptRegister(f) \/ WAdoptRead(f) \/ WAdoptStore(f)

\* Every bounded run ends here; keeps TLC's deadlock check meaningful.
Terminated == /\ bpc = "done"
              /\ \A f \in Files : wpc[f] = "idle" /\ (wOps[f] = MaxOps \/ LiveD(f) = {})
              /\ UNCHANGED vars

Next == Builder \/ (\E f \in Files : Writer(f)) \/ Terminated

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(* Properties.                                                             *)

TypeOK ==
    /\ dstate \in {"NONE", "BUILDING", "BUILT"}
    /\ \A f \in Files : rec[f].a \in [A -> 0..1] /\ rec[f].het \in BOOLEAN
                        /\ rec[f].path \in Names \cup {NoName}
    /\ reg \in [Procs -> BOOLEAN] /\ dirty \in [Procs -> BOOLEAN]

\* No update in flight: the builder is not inside an attempt and every writer has
\* finished its hook (and its adoption). Between a lower change and its hook the
\* index lags by design, as in any write-behind of the lower call.
Quiescent == bpc \in {"start", "done"} /\ \A f \in Files : wpc[f] = "idle"

\* "A file is absent from the index or whole in it" (spec section 9): once the domain
\* is BUILT and nothing is in flight, every file has exactly its lower attributes.
\* A lost hook update, or a stale reading stored over it, violates this.
NoLostUpdate ==
    (dstate = "BUILT" /\ Quiescent) =>
        \A f \in Files : rec[f].a = IF LiveD(f) # {} THEN low[f].a ELSE Zero

\* A record answered as homogeneous really passes target_ok() now (spec section 3):
\* a classification computed before a registration must not outlive a chmod.
NoStaleHomogeneity ==
    (dstate = "BUILT" /\ Quiescent) =>
        \A f \in Files : (Present(rec[f]) /\ ~rec[f].het) => Ok(f)

\* A homogeneous record carries a name the file has now (QUERY2 resolves it and
\* silently omits the file otherwise; R2-2).
FreshPath ==
    (dstate = "BUILT" /\ Quiescent) =>
        \A f \in Files : (Present(rec[f]) /\ ~rec[f].het) => rec[f].path \in NamesOf(f)

\* Stronger, for observation only (not part of the S21 acceptance; see README):
\* every record, heterogeneous ones too, is classified exactly and carries a live name.
ExactRecord ==
    (dstate = "BUILT" /\ Quiescent) =>
        \A f \in Files : Present(rec[f]) =>
            /\ rec[f].het = ~Ok(f)
            /\ rec[f].path \in NamesOf(f)
=============================================================================
