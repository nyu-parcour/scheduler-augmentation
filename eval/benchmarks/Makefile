PROJECT_ROOT := $(shell git rev-parse --show-toplevel)
INCLUDE      := $(PROJECT_ROOT)/include

CC:=g++
CFLAGS = -O2 -march=native -std=c++17 -I$(INCLUDE) -pthread

BIN_DIR := bin
RESULT_DIR := result
INPUT_DIR := input
GEN_GRAPH_DIR := summary_plots

# Number of rounds each benchmark is run for a particular thread count
ROUNDS := 1 

# GRAPH       := bfs triangle_count maximal_independent_set low_diameter_decomposition
GRAPH       := triangle_count low_diameter_decomposition
# GEOMETRY    := knn delaunay quickhull
# STRING      := kmp huffman_tree suffix_array
STRING      := kmp huffman_tree
SORTING     := mergesort quicksort
NUMERICAL   := mcss primes karatsuba

BASE      = $(basename $*)

CATEGORY  = $(strip \
  $(if $(filter $(BASE), $(GRAPH)),graph, \
  $(if $(filter $(BASE), $(GEOMETRY)),geometry, \
  $(if $(filter $(BASE), $(SORTING)),sorting, \
  $(if $(filter $(BASE), $(STRING)),string, \
  $(if $(filter $(BASE), $(NUMERICAL)),numerical,unknown))))))

OUTFILE = $(RESULT_DIR)/$(CATEGORY)_$* 


SUITE := $(GRAPH) $(GEOMETRY) $(SORTING) $(STRING) $(NUMERICAL)
AUG_SUITE := $(addsuffix .aug, $(SUITE))

bfs_ARG := 2000000
triangle_count_ARG := 800000
maximal_independent_set_ARG := 7000000
low_diameter_decomposition_ARG := 10000000
knn_ARG := 500000
delaunay_ARG := 100000
quickhull_ARG := 50000000
kmp_ARG := helloworld $(INPUT_DIR)/longtext.txt
huffman_tree_ARG := 3000000
suffix_array_ARG := $(INPUT_DIR)/longtext.txt
mergesort_ARG := 10000000
quicksort_ARG := 10000000
mcss_ARG := 1000000000
primes_ARG := 800000000
karatsuba_ARG := 10000000

# Standard binary definitions
BIN_GRAPH := $(addprefix $(BIN_DIR)/, $(GRAPH))
BIN_GEOMETRY := $(addprefix $(BIN_DIR)/, $(GEOMETRY))
BIN_STRING := $(addprefix $(BIN_DIR)/, $(STRING))
BIN_SORTING := $(addprefix $(BIN_DIR)/, $(SORTING))
BIN_NUMERICAL := $(addprefix $(BIN_DIR)/, $(NUMERICAL))

BIN_GRAPH_AUG := $(addsuffix .aug, $(BIN_GRAPH))
BIN_GEOMETRY_AUG := $(addsuffix .aug, $(BIN_GEOMETRY))
BIN_STRING_AUG := $(addsuffix .aug, $(BIN_STRING))
BIN_SORTING_AUG := $(addsuffix .aug, $(BIN_SORTING))
BIN_NUMERICAL_AUG := $(addsuffix .aug, $(BIN_NUMERICAL))

.PHONY: clean run suite suite.aug FORCE
FORCE:

suite: $(BIN_GRAPH) $(BIN_GEOMETRY) $(BIN_STRING) $(BIN_SORTING) $(BIN_NUMERICAL) $(BIN_GRAPH_AUG) $(BIN_GEOMETRY_AUG) $(BIN_STRING_AUG) $(BIN_SORTING_AUG) $(BIN_NUMERICAL_AUG)
run: $(addprefix $(RESULT_DIR)/, $(sort $(SUITE) $(AUG_SUITE))) gen_graphs

gen_graphs: $(GEN_GRAPH_DIR) run
	python3 ../scripts/generate_overhead_table.py $(RESULT_DIR) > $(GEN_GRAPH_DIR)/parlay_table_1_40_80.txt
	python3 ../scripts/generate_overhead_table_all.py $(RESULT_DIR) > $(GEN_GRAPH_DIR)/parlay_table_all.txt
	python3 ../scripts/generate_overhead_graph.py $(RESULT_DIR) $(GEN_GRAPH_DIR)
	python3 ../scripts/generate_speedup_subplots.py $(RESULT_DIR) $(GEN_GRAPH_DIR)

$(BIN_DIR) $(RESULT_DIR) $(INPUT_DIR) $(GEN_GRAPH_DIR):
	mkdir -p $@

$(INPUT_DIR)/longtext.txt: | $(INPUT_DIR)
	python3 ../scripts/generate_string_benchmark.py 100 > $@

# Rules for standard binaries
$(BIN_GRAPH): | $(BIN_DIR)
	$(CC) $(CFLAGS) graph/$(notdir $@)/$(notdir $@).cpp -o $@

$(BIN_GEOMETRY): | $(BIN_DIR)
	$(CC) $(CFLAGS) geometry/$(notdir $@)/$(notdir $@).cpp -o $@

$(BIN_STRING): $(INPUT_DIR)/longtext.txt | $(BIN_DIR)
	$(CC) $(CFLAGS) string/$(notdir $@)/$(notdir $@).cpp -o $@

$(BIN_SORTING): | $(BIN_DIR)
	$(CC) $(CFLAGS) sorting/$(notdir $@)/$(notdir $@).cpp -o $@

$(BIN_NUMERICAL): | $(BIN_DIR)
	$(CC) $(CFLAGS) numerical/$(notdir $@)/$(notdir $@).cpp -o $@

$(BIN_GRAPH_AUG): | $(BIN_DIR)
	$(CC) $(CFLAGS) -DPARLAY_AUG graph/$(basename $(notdir $@))/$(basename $(notdir $@)).cpp -o $@

$(BIN_GEOMETRY_AUG): | $(BIN_DIR)
	$(CC) $(CFLAGS) -DPARLAY_AUG geometry/$(basename $(notdir $@))/$(basename $(notdir $@)).cpp -o $@

$(BIN_STRING_AUG): | $(BIN_DIR)
	$(CC) $(CFLAGS) -DPARLAY_AUG string/$(basename $(notdir $@))/$(basename $(notdir $@)).cpp -o $@

$(BIN_SORTING_AUG): | $(BIN_DIR)
	$(CC) $(CFLAGS) -DPARLAY_AUG sorting/$(basename $(notdir $@))/$(basename $(notdir $@)).cpp -o $@

$(BIN_NUMERICAL_AUG): | $(BIN_DIR)
	$(CC) $(CFLAGS) -DPARLAY_AUG numerical/$(basename $(notdir $@))/$(basename $(notdir $@)).cpp -o $@

$(RESULT_DIR)/%: $(BIN_DIR)/% FORCE | $(RESULT_DIR)
	$(eval TMP_OUT := $(strip $(OUTFILE)).tmp)
	rm -f $(OUTFILE)
	rm -f $(TMP_OUT)

	# --- This block is for NON-AUG runs ---
	if [ "$(suffix $@)" != ".aug" ]; then \
		for thread in 1 10 20 30 40 50 60 70 80; do \
			export PARLAY_NUM_THREADS=$$thread; \
			for i in $(shell seq 1 $(ROUNDS)); do \
				TIME=$$(numactl -i all $< $($(BASE)_ARG) | tr -d '\n'); \
				printf "Threads:$$thread,Time:$$TIME\n" >> $(TMP_OUT); \
			done; \
		done; \
		python3 ../scripts/parse_logs.py < $(TMP_OUT) > $(OUTFILE); \
		rm $(TMP_OUT); \
	fi

	# --- This block is for AUG runs ---
	if [ "$(suffix $@)" = ".aug" ]; then \
		rm -f logs_vertex.txt; \
		for thread in 1 10 20 30 40 50 60 70 80; do \
			export PARLAY_NUM_THREADS=$$thread; \
			for i in $(shell seq 1 $(ROUNDS)); do \
				TIME=$$(numactl -i all $< $($(BASE)_ARG) | tr -d '\n'); \
				printf "Threads:$$thread,Time:$$TIME\n" >> $(TMP_OUT); \
			done; \
		done; \
		python3 ../scripts/parse_logs.py < $(TMP_OUT) >> $(OUTFILE); \
		rm $(TMP_OUT); \
		python3 ../scripts/parse_logs.py < logs_vertex.txt >> $(OUTFILE); \
		rm logs_vertex.txt; \
	fi

clean:
	rm -rf $(BIN_DIR) $(RESULT_DIR) $(INPUT_DIR) $(GEN_GRAPH_DIR)