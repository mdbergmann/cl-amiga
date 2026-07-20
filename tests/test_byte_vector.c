/*
 * Tests for packed byte vectors — TYPE_BYTE_VECTOR, the true 1-byte-per-
 * element storage behind (unsigned-byte 8) / (signed-byte 8) arrays.
 *
 * Specified behavior follows the HyperSpec for make-array / aref / elt /
 * fill / subseq / array-element-type / upgraded-array-element-type / typep
 * on specialized arrays; where clamiga deliberately restricts a feature
 * (vector-push-extend / adjust-array on packed storage), the tests pin the
 * clean-error behavior so it never degrades into silent corruption.
 */
#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "core/fasl.h"
#include "platform/platform.h"

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Helper: eval a string, return printed result */
static const char *eval_print(const char *str)
{
    static char buf[1024];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        return buf;
    }
}

/* ========================================================
 * Step 1: Allocation, layout, predicate, GC
 * ======================================================== */

TEST(alloc_zero)
{
    CL_Obj bv = cl_make_byte_vector(0, 0);
    ASSERT(CL_BYTE_VECTOR_P(bv));
    ASSERT_EQ_INT(0, (int)((CL_ByteVector *)CL_OBJ_TO_PTR(bv))->length);
}

TEST(alloc_len_and_zero_fill)
{
    CL_Obj bv = cl_make_byte_vector(100, 0);
    CL_ByteVector *b = (CL_ByteVector *)CL_OBJ_TO_PTR(bv);
    uint32_t i;
    ASSERT_EQ_INT(100, (int)b->length);
    for (i = 0; i < 100; i++)
        ASSERT_EQ_INT(0, (int)b->data[i]);
}

TEST(packed_size_is_one_byte_per_element)
{
    /* The whole point: header + N bytes, not header + 4N.  Guard against a
     * regression that silently reverts to CL_Obj storage. */
    CL_Obj bv = cl_make_byte_vector(64, 0);
    CL_ByteVector *b = (CL_ByteVector *)CL_OBJ_TO_PTR(bv);
    ASSERT(CL_HDR_SIZE(b) < sizeof(CL_ByteVector) + 64 + CL_ALIGN);
}

TEST(signedness_flag)
{
    CL_Obj u = cl_make_byte_vector(4, 0);
    CL_Obj s = cl_make_byte_vector(4, 1);
    ASSERT_EQ_INT(0, (int)((CL_ByteVector *)CL_OBJ_TO_PTR(u))->is_signed);
    ASSERT_EQ_INT(1, (int)((CL_ByteVector *)CL_OBJ_TO_PTR(s))->is_signed);
}

TEST(signed_get_sign_extends)
{
    CL_Obj s = cl_make_byte_vector(2, 1);
    CL_ByteVector *b = (CL_ByteVector *)CL_OBJ_TO_PTR(s);
    b->data[0] = 0xFF;   /* -1 as (signed-byte 8) */
    b->data[1] = 0x80;   /* -128 */
    ASSERT_EQ_INT(-1, (int)cl_bytevec_get(b, 0));
    ASSERT_EQ_INT(-128, (int)cl_bytevec_get(b, 1));
    /* Same raw bytes read unsigned */
    b = (CL_ByteVector *)CL_OBJ_TO_PTR(cl_make_byte_vector(1, 0));
    b->data[0] = 0xFF;
    ASSERT_EQ_INT(255, (int)cl_bytevec_get(b, 0));
}

TEST(predicate_negative)
{
    ASSERT(!CL_BYTE_VECTOR_P(CL_NIL));
    ASSERT(!CL_BYTE_VECTOR_P(CL_MAKE_FIXNUM(42)));
    ASSERT(!CL_BYTE_VECTOR_P(cl_make_vector(3)));
    ASSERT(!CL_BYTE_VECTOR_P(cl_make_bit_vector(3)));
    ASSERT(!CL_VECTOR_P(cl_make_byte_vector(3, 0)));
    ASSERT(!CL_BIT_VECTOR_P(cl_make_byte_vector(3, 0)));
}

TEST(gc_survives_with_contents)
{
    CL_Obj bv = cl_make_byte_vector(64, 0);
    CL_ByteVector *b = (CL_ByteVector *)CL_OBJ_TO_PTR(bv);
    b->data[0] = 255;
    b->data[63] = 7;

    CL_GC_PROTECT(bv);
    cl_gc();
    CL_GC_UNPROTECT(1);

    ASSERT(CL_BYTE_VECTOR_P(bv));
    b = (CL_ByteVector *)CL_OBJ_TO_PTR(bv);
    ASSERT_EQ_INT(255, (int)b->data[0]);
    ASSERT_EQ_INT(7, (int)b->data[63]);
}

/* ========================================================
 * Step 2: MAKE-ARRAY routing + init options
 * ======================================================== */

TEST(make_array_u8_is_packed)
{
    CL_Obj v = cl_eval_string("(make-array 8 :element-type '(unsigned-byte 8))");
    ASSERT(CL_BYTE_VECTOR_P(v));
    ASSERT_EQ_INT(0, (int)((CL_ByteVector *)CL_OBJ_TO_PTR(v))->is_signed);
}

TEST(make_array_s8_is_packed)
{
    CL_Obj v = cl_eval_string("(make-array 8 :element-type '(signed-byte 8))");
    ASSERT(CL_BYTE_VECTOR_P(v));
    ASSERT_EQ_INT(1, (int)((CL_ByteVector *)CL_OBJ_TO_PTR(v))->is_signed);
}

TEST(make_array_upgraded_specs)
{
    /* (mod 256), (integer 0 255) upgrade to u8; (integer -5 5) to s8;
     * (unsigned-byte 1) stays a bit-vector (more specialized). */
    ASSERT(CL_BYTE_VECTOR_P(cl_eval_string(
        "(make-array 4 :element-type '(mod 256))")));
    ASSERT(CL_BYTE_VECTOR_P(cl_eval_string(
        "(make-array 4 :element-type '(integer 0 255))")));
    ASSERT(CL_BYTE_VECTOR_P(cl_eval_string(
        "(make-array 4 :element-type '(integer -5 5))")));
    ASSERT(CL_BIT_VECTOR_P(cl_eval_string(
        "(make-array 4 :element-type '(unsigned-byte 1))")));
    /* (unsigned-byte 16) does NOT fit — stays a general vector */
    ASSERT(CL_VECTOR_P(cl_eval_string(
        "(make-array 4 :element-type '(unsigned-byte 16))")));
}

TEST(make_array_initial_element)
{
    ASSERT_STR_EQ(eval_print(
        "(make-array 3 :element-type '(unsigned-byte 8) :initial-element 9)"),
        "#(9 9 9)");
}

TEST(make_array_initial_contents)
{
    ASSERT_STR_EQ(eval_print(
        "(make-array 4 :element-type '(unsigned-byte 8) "
        ":initial-contents '(0 1 128 255))"),
        "#(0 1 128 255)");
    ASSERT_STR_EQ(eval_print(
        "(make-array 3 :element-type '(signed-byte 8) "
        ":initial-contents '(-128 0 127))"),
        "#(-128 0 127)");
}

TEST(make_array_initial_element_range_error)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (make-array 2 :element-type '(unsigned-byte 8) "
        ":initial-element 256) (type-error () :caught))"),
        ":CAUGHT");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (make-array 2 :element-type '(unsigned-byte 8) "
        ":initial-element -1) (type-error () :caught))"),
        ":CAUGHT");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (make-array 2 :element-type '(signed-byte 8) "
        ":initial-element 128) (type-error () :caught))"),
        ":CAUGHT");
}

TEST(make_array_initial_contents_range_error)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (make-array 2 :element-type '(unsigned-byte 8) "
        ":initial-contents '(1 300)) (type-error () :caught))"),
        ":CAUGHT");
}

TEST(make_array_dims_list_spelling)
{
    /* '(n) one-element dimension list must behave like n */
    ASSERT(CL_BYTE_VECTOR_P(cl_eval_string(
        "(make-array '(6) :element-type '(unsigned-byte 8))")));
}

/* ========================================================
 * Step 3: AREF / (SETF AREF) / ELT / bounds / range
 * ======================================================== */

TEST(aref_setf_aref)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 3 :element-type '(unsigned-byte 8))))"
        "  (setf (aref v 0) 255 (aref v 2) 1)"
        "  (list (aref v 0) (aref v 1) (aref v 2)))"),
        "(255 0 1)");
}

TEST(aref_signed_roundtrip)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 2 :element-type '(signed-byte 8))))"
        "  (setf (aref v 0) -128 (aref v 1) 127)"
        "  (list (aref v 0) (aref v 1)))"),
        "(-128 127)");
}

TEST(aref_bounds_error)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (aref (make-array 2 :element-type '(unsigned-byte 8)) 2)"
        " (error () :caught))"),
        ":CAUGHT");
}

TEST(setf_aref_range_error)
{
    /* Both the builtin path and the OP_ASET fast path (constant index in
     * compiled code) must reject out-of-range stores. */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (let ((v (make-array 2 :element-type '(unsigned-byte 8))))"
        "  (setf (aref v 0) 300)) (error () :caught))"),
        ":CAUGHT");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (let ((v (make-array 2 :element-type '(signed-byte 8))))"
        "  (setf (aref v 0) 200)) (error () :caught))"),
        ":CAUGHT");
}

TEST(elt_and_setf_elt)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 3 :element-type '(unsigned-byte 8)"
        " :initial-contents '(7 8 9))))"
        "  (setf (elt v 1) 42)"
        "  (list (elt v 0) (elt v 1) (elt v 2)))"),
        "(7 42 9)");
}

TEST(row_major_aref)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 3 :element-type '(unsigned-byte 8))))"
        "  (setf (row-major-aref v 1) 5)"
        "  (row-major-aref v 1))"),
        "5");
}

/* ========================================================
 * Step 4: array queries / predicates / types
 * ======================================================== */

TEST(array_element_type)
{
    ASSERT_STR_EQ(eval_print(
        "(array-element-type (make-array 2 :element-type '(unsigned-byte 8)))"),
        "(UNSIGNED-BYTE 8)");
    ASSERT_STR_EQ(eval_print(
        "(array-element-type (make-array 2 :element-type '(signed-byte 8)))"),
        "(SIGNED-BYTE 8)");
}

TEST(upgraded_array_element_type)
{
    ASSERT_STR_EQ(eval_print("(upgraded-array-element-type '(unsigned-byte 8))"),
                  "(UNSIGNED-BYTE 8)");
    ASSERT_STR_EQ(eval_print("(upgraded-array-element-type '(mod 256))"),
                  "(UNSIGNED-BYTE 8)");
    ASSERT_STR_EQ(eval_print("(upgraded-array-element-type '(signed-byte 8))"),
                  "(SIGNED-BYTE 8)");
    ASSERT_STR_EQ(eval_print("(upgraded-array-element-type '(integer 0 100))"),
                  "(UNSIGNED-BYTE 8)");
}

TEST(predicates_and_queries)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 5 :element-type '(unsigned-byte 8))))"
        "  (list (vectorp v) (arrayp v) (simple-vector-p v)"
        "        (array-rank v) (array-dimensions v) (array-total-size v)"
        "        (array-dimension v 0) (array-in-bounds-p v 4)"
        "        (array-in-bounds-p v 5) (length v)))"),
        "(T T NIL 1 (5) 5 5 T NIL 5)");
}

TEST(typep_discrimination)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 2 :element-type '(unsigned-byte 8))))"
        "  (list (typep v 'vector) (typep v 'array) (typep v 'sequence)"
        "        (typep v '(vector (unsigned-byte 8)))"
        "        (typep v '(vector (signed-byte 8)))"
        "        (typep v '(vector t))"
        "        (typep v 'simple-vector)"
        "        (typep v 'bit-vector) (typep v 'string)))"),
        "(T T T T NIL NIL NIL NIL NIL)");
    /* general T vector must not satisfy (vector (unsigned-byte 8)) */
    ASSERT_STR_EQ(eval_print(
        "(typep (make-array 2) '(vector (unsigned-byte 8)))"), "NIL");
}

TEST(type_of_roundtrips_through_typep)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 3 :element-type '(unsigned-byte 8))))"
        "  (typep v (type-of v)))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(type-of (make-array 3 :element-type '(unsigned-byte 8)))"),
        "(VECTOR (UNSIGNED-BYTE 8) 3)");
}

/* ========================================================
 * Step 5: fill pointer / vector-push / vector-pop
 * ======================================================== */

TEST(fill_pointer_push_pop)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 3 :element-type '(unsigned-byte 8) :fill-pointer 0)))"
        "  (list (vector-push 10 v) (vector-push 20 v) (fill-pointer v)"
        "        (length v) (vector-pop v) (fill-pointer v)))"),
        "(0 1 2 2 20 1)");
}

TEST(vector_push_full_returns_nil)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 1 :element-type '(unsigned-byte 8) :fill-pointer 1)))"
        "  (vector-push 1 v))"),
        "NIL");
}

TEST(vector_push_extend_full_errors_cleanly)
{
    /* NON-adjustable packed storage cannot be extended in place — must be a
     * clean error, never a silently different object.  (Adjustable byte
     * arrays use the annotated general-vector representation and extend
     * fine — see adjustable_byte_arrays_grow.) */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (let ((v (make-array 1 :element-type '(unsigned-byte 8)"
        " :fill-pointer 1))) (vector-push-extend 2 v))"
        " (error () :caught))"),
        ":CAUGHT");
}

TEST(adjust_array_fresh_copy)
{
    /* ADJUST-ARRAY on a packed (non-adjustable) byte vector returns a FRESH
     * byte vector per CLHS — contents copied, growth from :initial-element,
     * fill pointer carried over. */
    ASSERT_STR_EQ(eval_print(
        "(let* ((v (make-array 3 :element-type '(unsigned-byte 8)"
        "             :initial-contents '(1 2 3)))"
        "       (w (adjust-array v 5 :initial-element 9)))"
        "  (list (eq v w) w (array-element-type w)))"),
        "(NIL #(1 2 3 9 9) (UNSIGNED-BYTE 8))");
    ASSERT_STR_EQ(eval_print(
        "(adjust-array (make-array 4 :element-type '(unsigned-byte 8)"
        " :initial-contents '(1 2 3 4)) 2)"),
        "#(1 2)");
}

TEST(adjustable_byte_arrays_grow)
{
    /* :adjustable t byte arrays use the annotated general-vector
     * representation so ADJUST-ARRAY / VECTOR-PUSH-EXTEND grow them IN
     * PLACE (identity preserved) — the drakma/chunga HTTP-buffer idiom.
     * They still report the byte element type and satisfy the same typep. */
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 4 :element-type '(unsigned-byte 8)"
        "            :adjustable t :fill-pointer 0)))"
        "  (dotimes (i 3) (vector-push-extend i v))"
        "  (let ((w (adjust-array v 8)))"
        "    (dotimes (i 5) (vector-push-extend (+ 10 i) v))"
        "    (list (eq v w) (adjustable-array-p v)"
        "          (array-element-type v)"
        "          (typep v '(vector (unsigned-byte 8)))"
        "          (typep v '(vector t))"
        "          (length v) (aref v 0) (aref v 7))))"),
        "(T T (UNSIGNED-BYTE 8) T NIL 8 0 14)");
    /* the adjustable representation is NOT packed */
    CL_Obj v = cl_eval_string(
        "(make-array 4 :element-type '(unsigned-byte 8) :adjustable t)");
    ASSERT(!CL_BYTE_VECTOR_P(v));
    ASSERT(CL_VECTOR_P(v));
}

/* ========================================================
 * Step 6: sequence functions
 * ======================================================== */

TEST(seq_fill)
{
    ASSERT_STR_EQ(eval_print(
        "(fill (make-array 4 :element-type '(unsigned-byte 8)) 7)"),
        "#(7 7 7 7)");
    ASSERT_STR_EQ(eval_print(
        "(fill (make-array 4 :element-type '(unsigned-byte 8)"
        " :initial-element 1) 9 :start 1 :end 3)"),
        "#(1 9 9 1)");
}

TEST(seq_subseq)
{
    ASSERT_STR_EQ(eval_print(
        "(subseq (make-array 5 :element-type '(unsigned-byte 8)"
        " :initial-contents '(3 1 4 1 5)) 1 4)"),
        "#(1 4 1)");
    ASSERT_STR_EQ(eval_print(
        "(array-element-type (subseq (make-array 2 :element-type"
        " '(unsigned-byte 8)) 0))"),
        "(UNSIGNED-BYTE 8)");
}

TEST(seq_setf_subseq)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 5 :element-type '(unsigned-byte 8))))"
        "  (setf (subseq v 1 4) (make-array 3 :element-type '(unsigned-byte 8)"
        "                          :initial-contents '(9 8 7)))"
        "  v)"),
        "#(0 9 8 7 0)");
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 3 :element-type '(unsigned-byte 8))))"
        "  (setf (subseq v 0) '(1 2 3)) v)"),
        "#(1 2 3)");
}

TEST(seq_copy_reverse_nreverse)
{
    ASSERT_STR_EQ(eval_print(
        "(let* ((v (make-array 3 :element-type '(unsigned-byte 8)"
        "            :initial-contents '(1 2 3)))"
        "       (c (copy-seq v)))"
        "  (list (reverse v) v (eq c v) (array-element-type c)))"),
        "(#(3 2 1) #(1 2 3) NIL (UNSIGNED-BYTE 8))");
    ASSERT_STR_EQ(eval_print(
        "(nreverse (make-array 3 :element-type '(unsigned-byte 8)"
        " :initial-contents '(1 2 3)))"),
        "#(3 2 1)");
}

TEST(seq_scans)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 5 :element-type '(unsigned-byte 8)"
        " :initial-contents '(3 1 4 1 5))))"
        "  (list (position 4 v) (count 1 v) (find 5 v)"
        "        (some #'evenp v) (every #'integerp v) (reduce #'+ v)))"),
        "(2 2 5 T T 14)");
}

TEST(seq_sort_replace_concatenate)
{
    ASSERT_STR_EQ(eval_print(
        "(sort (make-array 5 :element-type '(unsigned-byte 8)"
        " :initial-contents '(3 1 4 1 5)) #'<)"),
        "#(1 1 3 4 5)");
    ASSERT_STR_EQ(eval_print(
        "(replace (make-array 3 :element-type '(unsigned-byte 8))"
        " (make-array 3 :element-type '(unsigned-byte 8)"
        " :initial-contents '(1 2 3)))"),
        "#(1 2 3)");
    ASSERT_STR_EQ(eval_print(
        "(concatenate 'list (make-array 2 :element-type '(unsigned-byte 8)"
        " :initial-contents '(1 2)) '(3))"),
        "(1 2 3)");
}

TEST(seq_map_and_map_into)
{
    ASSERT_STR_EQ(eval_print(
        "(map 'list #'1+ (make-array 3 :element-type '(unsigned-byte 8)"
        " :initial-contents '(1 2 3)))"),
        "(2 3 4)");
    ASSERT_STR_EQ(eval_print(
        "(map-into (make-array 3 :element-type '(unsigned-byte 8))"
        " #'+ #(1 2 3) #(10 20 30))"),
        "#(11 22 33)");
}

TEST(coerce_roundtrip)
{
    ASSERT_STR_EQ(eval_print(
        "(coerce (make-array 3 :element-type '(unsigned-byte 8)"
        " :initial-contents '(1 2 3)) 'list)"),
        "(1 2 3)");
    ASSERT_STR_EQ(eval_print(
        "(array-element-type (coerce '(1 2 3) '(vector (unsigned-byte 8))))"),
        "(UNSIGNED-BYTE 8)");
    ASSERT_STR_EQ(eval_print(
        "(coerce '(-1 127) '(vector (signed-byte 8)))"),
        "#(-1 127)");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (coerce '(1 300) '(vector (unsigned-byte 8)))"
        " (type-error () :caught))"),
        ":CAUGHT");
}

/* ========================================================
 * Step 7: equality + hashing
 * ======================================================== */

TEST(equalp_content)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((a (make-array 3 :element-type '(unsigned-byte 8)"
        "           :initial-contents '(1 2 3)))"
        "      (b (make-array 3 :element-type '(unsigned-byte 8)"
        "           :initial-contents '(1 2 3))))"
        "  (list (equalp a b) (equal a b) (eq a b)))"),
        "(T NIL NIL)");
    /* EQUALP compares element values across specializations */
    ASSERT_STR_EQ(eval_print(
        "(equalp (make-array 2 :element-type '(unsigned-byte 8)"
        " :initial-contents '(1 2)) (vector 1 2))"),
        "T");
}

TEST(equalp_hash_table_keys)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((h (make-hash-table :test 'equalp)))"
        "  (setf (gethash (make-array 2 :element-type '(unsigned-byte 8)"
        "                   :initial-contents '(1 2)) h) :hit)"
        "  (gethash (make-array 2 :element-type '(unsigned-byte 8)"
        "             :initial-contents '(1 2)) h))"),
        ":HIT");
    /* Cross-type: a byte-vector key must also be findable via an
     * EQUALP-equal general vector or bit-vector, and vice versa (CLHS
     * EQUALP is defined over content, not concrete array type). */
    ASSERT_STR_EQ(eval_print(
        "(let ((h (make-hash-table :test 'equalp)))"
        "  (setf (gethash (make-array 2 :element-type '(unsigned-byte 8)"
        "                   :initial-contents '(1 2)) h) :from-bv)"
        "  (gethash (vector 1 2) h))"),
        ":FROM-BV");
    ASSERT_STR_EQ(eval_print(
        "(let ((h (make-hash-table :test 'equalp)))"
        "  (setf (gethash (vector 1 0) h) :from-vec)"
        "  (gethash (make-array 2 :element-type '(unsigned-byte 8)"
        "             :initial-contents '(1 0)) h))"),
        ":FROM-VEC");
    ASSERT_STR_EQ(eval_print(
        "(let ((h (make-hash-table :test 'equalp)))"
        "  (setf (gethash (make-array 2 :element-type '(unsigned-byte 8)"
        "                   :initial-contents '(1 0)) h) :from-bv-vs-bit)"
        "  (gethash #*10 h))"),
        ":FROM-BV-VS-BIT");
}

/* ========================================================
 * Step 8: printer
 * ======================================================== */

TEST(printer_forms)
{
    ASSERT_STR_EQ(eval_print(
        "(make-array 3 :element-type '(unsigned-byte 8)"
        " :initial-contents '(0 128 255))"),
        "#(0 128 255)");
    ASSERT_STR_EQ(eval_print(
        "(make-array 2 :element-type '(signed-byte 8)"
        " :initial-contents '(-1 -128))"),
        "#(-1 -128)");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-array* nil))"
        "  (prin1-to-string (make-array 2 :element-type '(unsigned-byte 8))))"),
        "\"#<BYTE-VECTOR>\"");
}

/* ========================================================
 * Step 9: FASL round trip
 * ======================================================== */

TEST(fasl_roundtrip)
{
    /* Serialize a byte vector through the FASL constant writer/reader. */
    extern void cl_fasl_writer_init(CL_FaslWriter *w, uint8_t *buf, uint32_t capacity);
    static uint8_t buf[256];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bv = cl_make_byte_vector(4, 1);
    CL_ByteVector *b = (CL_ByteVector *)CL_OBJ_TO_PTR(bv);
    CL_Obj back;
    b->data[0] = 0x00; b->data[1] = 0x7F; b->data[2] = 0x80; b->data[3] = 0xFF;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, bv);
    ASSERT_EQ_INT(FASL_OK, w.error);
    cl_fasl_writer_release(&w);

    cl_fasl_reader_init(&r, buf, w.pos);
    back = cl_fasl_deserialize_obj(&r);
    ASSERT_EQ_INT(FASL_OK, r.error);
    ASSERT(CL_BYTE_VECTOR_P(back));
    b = (CL_ByteVector *)CL_OBJ_TO_PTR(back);
    ASSERT_EQ_INT(4, (int)b->length);
    ASSERT_EQ_INT(1, (int)b->is_signed);
    ASSERT_EQ_INT(0x00, (int)b->data[0]);
    ASSERT_EQ_INT(0x7F, (int)b->data[1]);
    ASSERT_EQ_INT(0x80, (int)b->data[2]);
    ASSERT_EQ_INT(0xFF, (int)b->data[3]);
}

int main(void)
{
    setup();

    RUN(alloc_zero);
    RUN(alloc_len_and_zero_fill);
    RUN(packed_size_is_one_byte_per_element);
    RUN(signedness_flag);
    RUN(signed_get_sign_extends);
    RUN(predicate_negative);
    RUN(gc_survives_with_contents);

    RUN(make_array_u8_is_packed);
    RUN(make_array_s8_is_packed);
    RUN(make_array_upgraded_specs);
    RUN(make_array_initial_element);
    RUN(make_array_initial_contents);
    RUN(make_array_initial_element_range_error);
    RUN(make_array_initial_contents_range_error);
    RUN(make_array_dims_list_spelling);

    RUN(aref_setf_aref);
    RUN(aref_signed_roundtrip);
    RUN(aref_bounds_error);
    RUN(setf_aref_range_error);
    RUN(elt_and_setf_elt);
    RUN(row_major_aref);

    RUN(array_element_type);
    RUN(upgraded_array_element_type);
    RUN(predicates_and_queries);
    RUN(typep_discrimination);
    RUN(type_of_roundtrips_through_typep);

    RUN(fill_pointer_push_pop);
    RUN(vector_push_full_returns_nil);
    RUN(vector_push_extend_full_errors_cleanly);
    RUN(adjust_array_fresh_copy);
    RUN(adjustable_byte_arrays_grow);

    RUN(seq_fill);
    RUN(seq_subseq);
    RUN(seq_setf_subseq);
    RUN(seq_copy_reverse_nreverse);
    RUN(seq_scans);
    RUN(seq_sort_replace_concatenate);
    RUN(seq_map_and_map_into);
    RUN(coerce_roundtrip);

    RUN(equalp_content);
    RUN(equalp_hash_table_keys);

    RUN(printer_forms);

    RUN(fasl_roundtrip);

    teardown();
    REPORT();
}
