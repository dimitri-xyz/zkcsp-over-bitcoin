#include <libsnark/gadgetlib1/gadgets/hashes/sha256/sha256_gadget.hpp>
#include <algebra/fields/field_utils.hpp>
#include <libsnark/gadgetlib1/protoboard.hpp>

using namespace libsnark;

bool sha256_padding[256] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0};


template<typename FieldT>
class mydisjunction_gadget : public gadget<FieldT> {
private:
    pb_variable<FieldT> inv;
public:
    const pb_variable_array<FieldT> inputs;
    const pb_variable<FieldT> output;

    mydisjunction_gadget(protoboard<FieldT>& pb,
                       const pb_variable_array<FieldT> &inputs,
                       const pb_variable<FieldT> &output,
                       const std::string &annotation_prefix="") :
        gadget<FieldT>(pb, annotation_prefix), inputs(inputs), output(output)
    {
        assert(inputs.size() >= 1);
        inv.allocate(pb, FMT(this->annotation_prefix, " inv"));
    }

    void generate_r1cs_constraints();
    void generate_r1cs_witness();
};

template<typename FieldT>
class constraint_vars_protoboard : public protoboard<FieldT> {
	public:
		r1cs_constraint_system<FieldT> orig_cs;
	
		constraint_vars_protoboard() : protoboard<FieldT>() {}
    
    // vars should be unallocated
    void mk_constraints_vars(pb_variable_array<FieldT> &vars_aux, pb_variable_array<FieldT> &vars) {
			orig_cs = this->constraint_system;
			//printf("CS # before: %d\n", orig_cs.num_constraints());
			r1cs_constraint_system<FieldT> &cs = this->constraint_system;
			
			vars_aux.allocate(*this, cs.num_constraints(), "cs_vars_aux");
			vars.allocate(*this, cs.num_constraints(), "cs_vars");
			
			
			 for (size_t i = 0; i < cs.num_constraints(); ++i) {
				 r1cs_constraint<FieldT> &constr = cs.constraints[i];
				 
				 // Add an additional variable to all constraints.
				 // If the constraint is satisfied the variable should be zero
				
				 constr.c.add_term(vars_aux[i], 1); 
			 }
			 
			 auto num_cs = cs.num_constraints();
			 for (size_t i = 0; i < num_cs; ++i) {
				 this->add_r1cs_constraint(r1cs_constraint<FieldT>(vars_aux[i], -vars_aux[i], -vars[i]), "vars_aux and vars");
			 }
			 
		}
		
		void mk_witnesses(pb_variable_array<FieldT> &vars_aux, pb_variable_array<FieldT> &vars)
		{
			auto full_variable_assignment = this->full_variable_assignment();
			//printf("CS # after: %d\n", orig_cs.num_constraints());

			
			for (size_t c = 0; c < orig_cs.constraints.size(); ++c)
			{
				const FieldT ares = orig_cs.constraints[c].a.evaluate(full_variable_assignment);
				const FieldT bres = orig_cs.constraints[c].b.evaluate(full_variable_assignment);
				const FieldT cres = orig_cs.constraints[c].c.evaluate(full_variable_assignment);
				
				// res is zero iff constraint is satisfied
				const FieldT res = ares*bres-cres;
				
				// var is 0 iff constraint is satisfied	
				//this->val(vars[c]) = (res == FieldT::zero()) ? FieldT::zero() : FieldT::one();
				this->val(vars_aux[c]) = res;
				this->val(vars[c]) = (res  != FieldT::zero()) ? FieldT::one() :  FieldT::zero();
				//if (res != FieldT::zero())
				//	printf("---------- Found sudoku constraint unsatisfied! --------\n");
				
			}	

		}
    
};

template<typename FieldT>
class sudoku_gadget; 


template<typename FieldT>
class test_Maxwell : public gadget<FieldT> {
	private:
	pb_variable<FieldT> disjunction_out;
	
	public:
		const pb_variable<FieldT> &output;
		
		pb_variable_array<FieldT> cs_vars_aux, cs_vars;
		std::shared_ptr<sudoku_gadget<FieldT> > sudoku;
		std::shared_ptr<mydisjunction_gadget<FieldT> > disjunction;
		
		
		constraint_vars_protoboard<FieldT> &c_pb;
	
		test_Maxwell(constraint_vars_protoboard<FieldT> &pb, unsigned int n, const pb_variable<FieldT> &output);
		
    void generate_r1cs_constraints();
    void generate_r1cs_witness(std::vector<bit_vector> &puzzle_values,
                               std::vector<bit_vector> &input_solution_values,
                               bit_vector &input_seed_key,
                               bit_vector &hash_of_input_seed_key,
                               std::vector<bit_vector> &input_encrypted_solution);
                               
    const digest_variable<FieldT> &seed_key() { return *sudoku->seed_key; }
    const digest_variable<FieldT> &alleged_digest() { return *sudoku->h_seed_key; }

};

template<typename FieldT>
class sudoku_encryption_key : public gadget<FieldT> {
public:
    pb_variable_array<FieldT> seed_key; // (256-8) bit key
    unsigned int dimension;

    std::shared_ptr<digest_variable<FieldT>> padding_var;
    pb_linear_combination_array<FieldT> IV;

    std::vector<std::shared_ptr<digest_variable<FieldT>>> key; // dimension*dimension*8 bit key
    std::vector<pb_variable_array<FieldT>> salts;
    std::vector<std::shared_ptr<block_variable<FieldT>>> key_blocks;
    std::vector<std::shared_ptr<sha256_compression_function_gadget<FieldT>>> key_sha;

    sudoku_encryption_key(protoboard<FieldT> &pb,
                       unsigned int dimension,
                       pb_variable_array<FieldT> &seed_key
                       );
    void generate_r1cs_constraints();
    void generate_r1cs_witness();
};

template<typename FieldT>
class sudoku_cell_gadget : public gadget<FieldT> {
public:
    pb_linear_combination<FieldT> number;
    unsigned int dimension;

    /*
        This is an array of bits which indicates whether this
        cell is a particular number in the dimension. It is
        the size of the dimension N^2 of the puzzle. Only one
        bit is set.
    */
    pb_variable_array<FieldT> flags;

    sudoku_cell_gadget(protoboard<FieldT> &pb,
                       unsigned int dimension,
                       pb_linear_combination<FieldT> &number
                       );
    void generate_r1cs_constraints();
    void generate_r1cs_witness();
};

template<typename FieldT>
class sudoku_closure_gadget : public gadget<FieldT> {
public:
    unsigned int dimension;

    /*
        This is an array of bits which indicates whether this
        cell is a particular number in the dimension. It is
        the size of the dimension N^2 of the puzzle. Only one
        bit is set.
    */
    std::vector<pb_variable_array<FieldT>> flags;

    sudoku_closure_gadget(protoboard<FieldT> &pb,
                          unsigned int dimension,
                          std::vector<pb_variable_array<FieldT>> &flags
                         );
    void generate_r1cs_constraints();
    void generate_r1cs_witness();
};

template<typename FieldT>
class sudoku_gadget : public gadget<FieldT> {
public:
    unsigned int dimension;

    pb_variable_array<FieldT> input_as_field_elements; /* R1CS input */
    pb_variable_array<FieldT> input_as_bits; /* unpacked R1CS input */
    std::shared_ptr<multipacking_gadget<FieldT> > unpack_inputs; /* multipacking gadget */

    std::vector<pb_variable_array<FieldT>> puzzle_values;
    std::vector<pb_variable_array<FieldT>> solution_values;
    std::vector<pb_variable_array<FieldT>> encrypted_solution;

    std::vector<pb_linear_combination<FieldT>> puzzle_numbers;
    std::vector<pb_linear_combination<FieldT>> solution_numbers;

    std::vector<std::shared_ptr<sudoku_cell_gadget<FieldT>>> cells;

    std::vector<std::shared_ptr<sudoku_closure_gadget<FieldT>>> closure_rows;
    std::vector<std::shared_ptr<sudoku_closure_gadget<FieldT>>> closure_cols;
    std::vector<std::shared_ptr<sudoku_closure_gadget<FieldT>>> closure_groups;

    std::shared_ptr<digest_variable<FieldT>> seed_key;
    std::shared_ptr<digest_variable<FieldT>> h_seed_key;

    std::shared_ptr<block_variable<FieldT>> h_k_block;
    std::shared_ptr<sha256_compression_function_gadget<FieldT>> h_k_sha;
    std::shared_ptr<sudoku_encryption_key<FieldT>> key;

    pb_variable_array<FieldT> puzzle_enforce;
    
    bool look_at_digest;


    sudoku_gadget(protoboard<FieldT> &pb, unsigned int n, bool look_at_digest);
    void generate_r1cs_constraints();
    void generate_r1cs_witness(std::vector<bit_vector> &puzzle_values,
                               std::vector<bit_vector> &input_solution_values,
                               bit_vector &input_seed_key,
                               bit_vector &hash_of_input_seed_key,
                               std::vector<bit_vector> &input_encrypted_solution);
};

template<typename FieldT>
r1cs_primary_input<FieldT> sudoku_input_map(unsigned int n,
                                            std::vector<bit_vector> &puzzle_values,
                                            bit_vector &hash_of_input_seed_key,
                                            std::vector<bit_vector> &input_encrypted_solution
                                            );

#include "sudoku_gadget.tcc"
