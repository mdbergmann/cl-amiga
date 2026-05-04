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
 * Step 1: Allocation, predicate, GC
 * ======================================================== */

TEST(alloc_zero_bits)
{
    CL_Obj bv = cl_make_bit_vector(0);
    ASSERT(CL_BIT_VECTOR_P(bv));
    CL_BitVector *b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    ASSERT_EQ_INT(0, (int)b->length);
}

TEST(alloc_1_bit)
{
    CL_Obj bv = cl_make_bit_vector(1);
    ASSERT(CL_BIT_VECTOR_P(bv));
    CL_BitVector *b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    ASSERT_EQ_INT(1, (int)b->length);
    ASSERT_EQ_INT(0, (int)cl_bv_get_bit(b, 0));
}

TEST(alloc_32_bits)
{
    CL_Obj bv = cl_make_bit_vector(32);
    CL_BitVector *b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    ASSERT_EQ_INT(32, (int)b->length);
}

TEST(alloc_33_bits)
{
    CL_Obj bv = cl_make_bit_vector(33);
    CL_BitVector *b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    ASSERT_EQ_INT(33, (int)b->length);
}

TEST(alloc_100_bits)
{
    CL_Obj bv = cl_make_bit_vector(100);
    CL_BitVector *b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    ASSERT_EQ_INT(100, (int)b->length);
}

TEST(predicate_positive)
{
    CL_Obj bv = cl_make_bit_vector(8);
    ASSERT(CL_BIT_VECTOR_P(bv));
}

TEST(predicate_negative)
{
    ASSERT(!CL_BIT_VECTOR_P(CL_NIL));
    ASSERT(!CL_BIT_VECTOR_P(CL_MAKE_FIXNUM(42)));
    CL_Obj v = cl_make_vector(3);
    ASSERT(!CL_BIT_VECTOR_P(v));
}

TEST(gc_survives)
{
    CL_Obj bv = cl_make_bit_vector(64);
    CL_BitVector *b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    cl_bv_set_bit(b, 0, 1);
    cl_bv_set_bit(b, 63, 1);

    CL_GC_PROTECT(bv);
    cl_gc();
    CL_GC_UNPROTECT(1);

    b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    ASSERT(CL_BIT_VECTOR_P(bv));
    ASSERT_EQ_INT(1, (int)cl_bv_get_bit(b, 0));
    ASSERT_EQ_INT(1, (int)cl_bv_get_bit(b, 63));
}

/* ========================================================
 * Step 2: Reader and Printer
 * ======================================================== */

TEST(reader_empty)
{
    ASSERT_STR_EQ(eval_print("#*"), "#*");
}

TEST(reader_10110)
{
    ASSERT_STR_EQ(eval_print("#*10110"), "#*10110");
}

TEST(reader_32bit)
{
    ASSERT_STR_EQ(eval_print("#*10101010101010101010101010101010"),
                  "#*10101010101010101010101010101010");
}

TEST(reader_33bit)
{
    ASSERT_STR_EQ(eval_print("#*101010101010101010101010101010101"),
                  "#*101010101010101010101010101010101");
}

TEST(compiler_literal)
{
    ASSERT_STR_EQ(eval_print("(let ((x #*1100)) x)"), "#*1100");
}

/* ========================================================
 * Step 3: Predicates, type-of, typep, subtypep
 * ======================================================== */

TEST(eval_bit_vector_p)
{
    ASSERT_STR_EQ(eval_print("(bit-vector-p #*1010)"), "T");
    ASSERT_STR_EQ(eval_print("(bit-vector-p #())"), "NIL");
    ASSERT_STR_EQ(eval_print("(bit-vector-p 42)"), "NIL");
}

TEST(eval_simple_bit_vector_p)
{
    ASSERT_STR_EQ(eval_print("(simple-bit-vector-p #*1010)"), "T");
    ASSERT_STR_EQ(eval_print("(simple-bit-vector-p #())"), "NIL");
}

TEST(eval_type_of_bv)
{
    ASSERT_STR_EQ(eval_print("(type-of #*1010)"), "SIMPLE-BIT-VECTOR");
}

TEST(eval_typep_bit_vector)
{
    ASSERT_STR_EQ(eval_print("(typep #*1010 'bit-vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #*1010 'simple-bit-vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #*1010 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #*1010 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #*1010 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #*1010 'simple-array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #*1010 'string)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep #*1010 'integer)"), "NIL");
}

TEST(eval_vectorp_bv)
{
    ASSERT_STR_EQ(eval_print("(vectorp #*1010)"), "T");
}

TEST(eval_arrayp_bv)
{
    ASSERT_STR_EQ(eval_print("(arrayp #*1010)"), "T");
}

TEST(eval_subtypep_bv)
{
    ASSERT_STR_EQ(eval_print("(subtypep 'bit-vector 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'simple-bit-vector 'bit-vector)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'simple-bit-vector 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'simple-bit-vector 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'bit-vector 'sequence)"), "T");
}

/* ========================================================
 * Step 4: Element access (bit, sbit, aref, setf)
 * ======================================================== */

TEST(eval_bit_access)
{
    ASSERT_STR_EQ(eval_print("(bit #*10110 0)"), "1");
    ASSERT_STR_EQ(eval_print("(bit #*10110 1)"), "0");
    ASSERT_STR_EQ(eval_print("(bit #*10110 2)"), "1");
    ASSERT_STR_EQ(eval_print("(bit #*10110 3)"), "1");
    ASSERT_STR_EQ(eval_print("(bit #*10110 4)"), "0");
}

TEST(eval_sbit_access)
{
    ASSERT_STR_EQ(eval_print("(sbit #*1100 0)"), "1");
    ASSERT_STR_EQ(eval_print("(sbit #*1100 3)"), "0");
}

TEST(eval_aref_bv)
{
    ASSERT_STR_EQ(eval_print("(aref #*10110 2)"), "1");
    ASSERT_STR_EQ(eval_print("(aref #*10110 4)"), "0");
}

TEST(eval_setf_bit)
{
    ASSERT_STR_EQ(eval_print("(let ((bv #*0000)) (setf (bit bv 2) 1) bv)"), "#*0010");
}

TEST(eval_setf_sbit)
{
    ASSERT_STR_EQ(eval_print("(let ((bv #*1111)) (setf (sbit bv 1) 0) bv)"), "#*1011");
}

TEST(eval_setf_aref_bv)
{
    ASSERT_STR_EQ(eval_print("(let ((bv #*0000)) (setf (aref bv 0) 1) bv)"), "#*1000");
}

TEST(eval_bit_bounds_error)
{
    /* Out of range should error */
    const char *r = eval_print("(bit #*10 5)");
    ASSERT(strncmp(r, "ERROR:", 6) == 0);
}

/* ========================================================
 * Step 5: make-array with :element-type bit
 * ======================================================== */

TEST(eval_make_array_bit)
{
    ASSERT_STR_EQ(eval_print("(bit-vector-p (make-array 5 :element-type 'bit))"), "T");
    ASSERT_STR_EQ(eval_print("(make-array 5 :element-type 'bit)"), "#*00000");
}

TEST(eval_make_array_bit_initial_element)
{
    ASSERT_STR_EQ(eval_print("(make-array 4 :element-type 'bit :initial-element 1)"), "#*1111");
}

TEST(eval_make_array_bit_initial_contents)
{
    ASSERT_STR_EQ(eval_print("(make-array 4 :element-type 'bit :initial-contents '(1 0 1 0))"), "#*1010");
}

TEST(eval_make_array_bit_fill_pointer)
{
    ASSERT_STR_EQ(eval_print("(let ((bv (make-array 8 :element-type 'bit :fill-pointer 3 :initial-element 1))) (length bv))"), "3");
}

TEST(eval_make_array_bit_displaced)
{
    /* Regression: previously (make-array N :element-type 'bit
     * :displaced-to <bv> :displaced-index-offset K) errored with
     * "displacement to bit-vectors not yet supported", which aborted
     * universe.lsp during the ANSI test bootstrap.  Current
     * implementation copies the requested window (no live view of the
     * source — see comment in builtins_array.c).  Source #*0111000110:
     * bits at offsets 3..7 are 1,0,0,0,1 → result #*10001. */
    ASSERT_STR_EQ(eval_print(
        "(make-array 5 :element-type 'bit :displaced-to #*0111000110 :displaced-index-offset 3)"),
        "#*10001");
    /* Zero offset, exact-fit window. */
    ASSERT_STR_EQ(eval_print(
        "(make-array 4 :element-type 'bit :displaced-to #*1100 :displaced-index-offset 0)"),
        "#*1100");
    /* Out-of-bounds window must error (CL_ERR_ARGS = 4), not silently truncate. */
    ASSERT_STR_EQ(eval_print(
        "(make-array 5 :element-type 'bit :displaced-to #*010 :displaced-index-offset 0)"),
        "ERROR:4");
}

/* ========================================================
 * Step 6: Sequence protocol
 * ======================================================== */

TEST(eval_length_bv)
{
    ASSERT_STR_EQ(eval_print("(length #*10110)"), "5");
    ASSERT_STR_EQ(eval_print("(length #*)"), "0");
}

TEST(eval_elt_bv)
{
    ASSERT_STR_EQ(eval_print("(elt #*10110 0)"), "1");
    ASSERT_STR_EQ(eval_print("(elt #*10110 4)"), "0");
}

TEST(eval_find_bv)
{
    ASSERT_STR_EQ(eval_print("(find 1 #*00100)"), "1");
    ASSERT_STR_EQ(eval_print("(find 1 #*00000)"), "NIL");
}

TEST(eval_position_bv)
{
    ASSERT_STR_EQ(eval_print("(position 1 #*00100)"), "2");
    ASSERT_STR_EQ(eval_print("(position 1 #*00000)"), "NIL");
}

TEST(eval_count_bv)
{
    ASSERT_STR_EQ(eval_print("(count 1 #*10110)"), "3");
    ASSERT_STR_EQ(eval_print("(count 0 #*10110)"), "2");
}

TEST(eval_copy_seq_bv)
{
    ASSERT_STR_EQ(eval_print("(copy-seq #*10110)"), "#*10110");
    ASSERT_STR_EQ(eval_print("(let ((a #*101) (b (copy-seq #*101))) (setf (bit a 0) 0) (list a b))"),
                  "(#*001 #*101)");
}

TEST(eval_reverse_bv)
{
    ASSERT_STR_EQ(eval_print("(reverse #*10110)"), "#*01101");
    ASSERT_STR_EQ(eval_print("(reverse #*)"), "#*");
}

TEST(eval_fill_bv)
{
    ASSERT_STR_EQ(eval_print("(let ((bv #*00000)) (fill bv 1) bv)"), "#*11111");
    ASSERT_STR_EQ(eval_print("(let ((bv #*11111)) (fill bv 0 :start 1 :end 3) bv)"), "#*10011");
}

TEST(eval_setf_elt_bv)
{
    ASSERT_STR_EQ(eval_print("(let ((bv #*0000)) (setf (elt bv 2) 1) bv)"), "#*0010");
}

/* ========================================================
 * Step 7: Equality
 * ======================================================== */

TEST(eval_equal_bv)
{
    ASSERT_STR_EQ(eval_print("(equal #*10110 #*10110)"), "T");
    ASSERT_STR_EQ(eval_print("(equal #*10110 #*10111)"), "NIL");
    ASSERT_STR_EQ(eval_print("(equal #*10110 #*1011)"), "NIL");
    ASSERT_STR_EQ(eval_print("(equal #* #*)"), "T");
}

TEST(eval_equal_bv_vs_vec)
{
    /* Different types are not equal */
    ASSERT_STR_EQ(eval_print("(equal #*101 #(1 0 1))"), "NIL");
}

/* ========================================================
 * Step 8: Coerce
 * ======================================================== */

TEST(eval_coerce_bv_to_list)
{
    ASSERT_STR_EQ(eval_print("(coerce #*10110 'list)"), "(1 0 1 1 0)");
}

TEST(eval_coerce_list_to_bv)
{
    ASSERT_STR_EQ(eval_print("(coerce '(1 0 1 1 0) 'bit-vector)"), "#*10110");
}

TEST(eval_coerce_bv_to_vector)
{
    ASSERT_STR_EQ(eval_print("(coerce #*101 'vector)"), "#(1 0 1)");
}

TEST(eval_coerce_roundtrip)
{
    ASSERT_STR_EQ(eval_print("(equal (coerce (coerce #*10110 'list) 'bit-vector) #*10110)"), "T");
}

/* ========================================================
 * Step 9: Bitwise operations
 * ======================================================== */

TEST(eval_bit_and)
{
    ASSERT_STR_EQ(eval_print("(bit-and #*1100 #*1010)"), "#*1000");
}

TEST(eval_bit_ior)
{
    ASSERT_STR_EQ(eval_print("(bit-ior #*1100 #*1010)"), "#*1110");
}

TEST(eval_bit_xor)
{
    ASSERT_STR_EQ(eval_print("(bit-xor #*1100 #*1010)"), "#*0110");
}

TEST(eval_bit_eqv)
{
    ASSERT_STR_EQ(eval_print("(bit-eqv #*1100 #*1010)"), "#*1001");
}

TEST(eval_bit_nand)
{
    ASSERT_STR_EQ(eval_print("(bit-nand #*1100 #*1010)"), "#*0111");
}

TEST(eval_bit_nor)
{
    ASSERT_STR_EQ(eval_print("(bit-nor #*1100 #*1010)"), "#*0001");
}

TEST(eval_bit_andc1)
{
    ASSERT_STR_EQ(eval_print("(bit-andc1 #*1100 #*1010)"), "#*0010");
}

TEST(eval_bit_andc2)
{
    ASSERT_STR_EQ(eval_print("(bit-andc2 #*1100 #*1010)"), "#*0100");
}

TEST(eval_bit_orc1)
{
    ASSERT_STR_EQ(eval_print("(bit-orc1 #*1100 #*1010)"), "#*1011");
}

TEST(eval_bit_orc2)
{
    ASSERT_STR_EQ(eval_print("(bit-orc2 #*1100 #*1010)"), "#*1101");
}

TEST(eval_bit_not)
{
    ASSERT_STR_EQ(eval_print("(bit-not #*10110)"), "#*01001");
}

TEST(eval_bit_and_in_place)
{
    ASSERT_STR_EQ(eval_print("(let ((a #*1100)) (bit-and a #*1010 t) a)"), "#*1000");
}

TEST(eval_bit_and_result)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((r #*0000)) (bit-and #*1100 #*1010 r) r)"), "#*1000");
}

TEST(eval_bit_ops_word_boundary)
{
    /* 33 bits — crosses 32-bit word boundary */
    ASSERT_STR_EQ(eval_print(
        "(bit-and #*111111111111111111111111111111111 "
        "#*100000000000000000000000000000001)"),
        "#*100000000000000000000000000000001");
}

TEST(eval_bit_ops_length_mismatch)
{
    const char *r = eval_print("(bit-and #*11 #*1111)");
    ASSERT(strncmp(r, "ERROR:", 6) == 0);
}

/* ========================================================
 * Step 10: Array query functions
 * ======================================================== */

TEST(eval_array_dimensions_bv)
{
    ASSERT_STR_EQ(eval_print("(array-dimensions #*10110)"), "(5)");
}

TEST(eval_array_rank_bv)
{
    ASSERT_STR_EQ(eval_print("(array-rank #*10110)"), "1");
}

TEST(eval_array_dimension_bv)
{
    ASSERT_STR_EQ(eval_print("(array-dimension #*10110 0)"), "5");
}

TEST(eval_array_total_size_bv)
{
    ASSERT_STR_EQ(eval_print("(array-total-size #*10110)"), "5");
}

TEST(eval_row_major_aref_bv)
{
    ASSERT_STR_EQ(eval_print("(row-major-aref #*10110 2)"), "1");
    ASSERT_STR_EQ(eval_print("(row-major-aref #*10110 4)"), "0");
}

TEST(eval_array_has_fill_pointer_p_bv)
{
    ASSERT_STR_EQ(eval_print("(array-has-fill-pointer-p #*1010)"), "NIL");
}

TEST(eval_adjustable_array_p_bv)
{
    ASSERT_STR_EQ(eval_print("(adjustable-array-p #*1010)"), "NIL");
}

TEST(eval_array_row_major_index_bv)
{
    ASSERT_STR_EQ(eval_print("(array-row-major-index #*10110 3)"), "3");
}

/* ========================================================
 * Step 11: Remove/remove-if/remove-if-not on bit-vectors
 * ======================================================== */

TEST(eval_remove_bv)
{
    /* Remove 0 from bit-vector — should keep only 1s */
    ASSERT_STR_EQ(eval_print("(remove 0 #*10101)"), "#*111");
    /* Remove 1 from bit-vector — should keep only 0s */
    ASSERT_STR_EQ(eval_print("(remove 1 #*10101)"), "#*00");
}

TEST(eval_remove_if_bv)
{
    /* Remove bits that are 0 */
    ASSERT_STR_EQ(eval_print(
        "(remove-if (lambda (b) (= b 0)) #*10101)"), "#*111");
    /* Remove bits that are 1 */
    ASSERT_STR_EQ(eval_print(
        "(remove-if (lambda (b) (= b 1)) #*10101)"), "#*00");
}

TEST(eval_remove_if_not_bv)
{
    /* Keep only 1s (remove those that are NOT 1) */
    ASSERT_STR_EQ(eval_print(
        "(remove-if-not (lambda (b) (= b 1)) #*10101)"), "#*111");
    /* Keep only 0s */
    ASSERT_STR_EQ(eval_print(
        "(remove-if-not (lambda (b) (= b 0)) #*10101)"), "#*00");
}

TEST(eval_remove_bv_empty)
{
    /* Remove from empty bit-vector */
    ASSERT_STR_EQ(eval_print("(remove 0 #*)"), "#*");
    ASSERT_STR_EQ(eval_print("(remove-if #'zerop #*)"), "#*");
    ASSERT_STR_EQ(eval_print("(remove-if-not #'zerop #*)"), "#*");
}

TEST(eval_remove_bv_all)
{
    /* Remove all elements */
    ASSERT_STR_EQ(eval_print("(remove 1 #*1111)"), "#*");
    ASSERT_STR_EQ(eval_print("(remove 0 #*0000)"), "#*");
}

TEST(eval_remove_bv_none)
{
    /* Remove nothing (no matches) */
    ASSERT_STR_EQ(eval_print("(remove 0 #*1111)"), "#*1111");
    ASSERT_STR_EQ(eval_print("(remove 1 #*0000)"), "#*0000");
}

TEST(eval_remove_bv_result_type)
{
    /* Result should be a bit-vector */
    ASSERT_STR_EQ(eval_print(
        "(bit-vector-p (remove 0 #*101))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(bit-vector-p (remove-if #'zerop #*101))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(bit-vector-p (remove-if-not #'zerop #*101))"), "T");
}

/* ======================================================== */

int main(void)
{
    setup();

    /* Step 1: Allocation */
    RUN(alloc_zero_bits);
    RUN(alloc_1_bit);
    RUN(alloc_32_bits);
    RUN(alloc_33_bits);
    RUN(alloc_100_bits);
    RUN(predicate_positive);
    RUN(predicate_negative);
    RUN(gc_survives);

    /* Step 2: Reader/Printer */
    RUN(reader_empty);
    RUN(reader_10110);
    RUN(reader_32bit);
    RUN(reader_33bit);
    RUN(compiler_literal);

    /* Step 3: Type predicates */
    RUN(eval_bit_vector_p);
    RUN(eval_simple_bit_vector_p);
    RUN(eval_type_of_bv);
    RUN(eval_typep_bit_vector);
    RUN(eval_vectorp_bv);
    RUN(eval_arrayp_bv);
    RUN(eval_subtypep_bv);

    /* Step 4: Element access */
    RUN(eval_bit_access);
    RUN(eval_sbit_access);
    RUN(eval_aref_bv);
    RUN(eval_setf_bit);
    RUN(eval_setf_sbit);
    RUN(eval_setf_aref_bv);
    RUN(eval_bit_bounds_error);

    /* Step 5: make-array */
    RUN(eval_make_array_bit);
    RUN(eval_make_array_bit_initial_element);
    RUN(eval_make_array_bit_initial_contents);
    RUN(eval_make_array_bit_fill_pointer);
    RUN(eval_make_array_bit_displaced);

    /* Step 6: Sequence protocol */
    RUN(eval_length_bv);
    RUN(eval_elt_bv);
    RUN(eval_find_bv);
    RUN(eval_position_bv);
    RUN(eval_count_bv);
    RUN(eval_copy_seq_bv);
    RUN(eval_reverse_bv);
    RUN(eval_fill_bv);
    RUN(eval_setf_elt_bv);

    /* Step 7: Equality */
    RUN(eval_equal_bv);
    RUN(eval_equal_bv_vs_vec);

    /* Step 8: Coerce */
    RUN(eval_coerce_bv_to_list);
    RUN(eval_coerce_list_to_bv);
    RUN(eval_coerce_bv_to_vector);
    RUN(eval_coerce_roundtrip);

    /* Step 9: Bitwise ops */
    RUN(eval_bit_and);
    RUN(eval_bit_ior);
    RUN(eval_bit_xor);
    RUN(eval_bit_eqv);
    RUN(eval_bit_nand);
    RUN(eval_bit_nor);
    RUN(eval_bit_andc1);
    RUN(eval_bit_andc2);
    RUN(eval_bit_orc1);
    RUN(eval_bit_orc2);
    RUN(eval_bit_not);
    RUN(eval_bit_and_in_place);
    RUN(eval_bit_and_result);
    RUN(eval_bit_ops_word_boundary);
    RUN(eval_bit_ops_length_mismatch);

    /* Step 10: Array queries */
    RUN(eval_array_dimensions_bv);
    RUN(eval_array_rank_bv);
    RUN(eval_array_dimension_bv);
    RUN(eval_array_total_size_bv);
    RUN(eval_row_major_aref_bv);
    RUN(eval_array_has_fill_pointer_p_bv);
    RUN(eval_adjustable_array_p_bv);
    RUN(eval_array_row_major_index_bv);

    /* Step 11: Remove/remove-if/remove-if-not on bit-vectors */
    RUN(eval_remove_bv);
    RUN(eval_remove_if_bv);
    RUN(eval_remove_if_not_bv);
    RUN(eval_remove_bv_empty);
    RUN(eval_remove_bv_all);
    RUN(eval_remove_bv_none);
    RUN(eval_remove_bv_result_type);

    teardown();
    REPORT();
}
