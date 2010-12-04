all: seven_seg

seven_seg_OBJECTS = \
	src/picture.o \
	src/seven_seg.o

clean_TARGETS += $(seven_seg_OBJECTS)

CXXFLAGS=-g -O2 -W -Wall
LDFLAGS=-g

# external dependencies
CXXFLAGS += -DHAVE_PANGOCAIRO
CXXFLAGS += `sdl-config --cflags`
CXXFLAGS += `pkg-config --cflags pangocairo`

seven_seg_LIBS +=  `sdl-config --libs`
seven_seg_LIBS += `pkg-config --libs pangocairo`

seven_seg: $(seven_seg_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(seven_seg_LIBS)

%.o : %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $^

clean:
	rm -f $(clean_TARGETS)

.PHONY: all clean
