# Host-side tests for the pure logic in src/ (no board, no Arduino).
# Built as C++11 on purpose: that is what the ESP32 Arduino core 2.x compiles with.
CXX      ?= g++
CXXFLAGS ?= -std=gnu++11 -Wall -Wextra -Wno-unused-function -O1 -I../../src
OUT      := out

TESTS := test_countdown test_agenda test_keycombo test_eyes test_default_layout test_logring test_weather test_weathericon

.PHONY: all test render clean
all: test

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%: %.cpp $(wildcard ../../src/*.h) | $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ $<

test: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do echo "== $$t"; ./$(OUT)/$$t || exit 1; done
	@echo "ALL HOST TESTS PASSED"

render: $(OUT)/render_eyes $(OUT)/render_weather
	./$(OUT)/render_eyes $(OUT)
	python3 -I make_sheet.py $(OUT)
	./$(OUT)/render_weather $(OUT)
	python3 -I make_weather_sheet.py $(OUT)

clean:
	rm -rf $(OUT)
