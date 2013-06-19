/*
Yam v0.1.0 by Jonathan Dearborn
Yam is a C++ wrapper for the basic functionality of libyaml.

The Yam source code is released under the Boost Software License v1.0.
See LICENSE for details.
*/

#ifndef __YAM_H__
#define __YAM_H__

#include "yaml.h"
#include <list>

typedef yaml_read_handler_t Yam_Read_Handler;
typedef yaml_write_handler_t Yam_Write_Handler;

class Yam
{
public:
    
    enum ParseResultEnum {DONE, OK, ERROR};
    enum EventType {NONE, BEGIN_SEQUENCE, END_SEQUENCE, BEGIN_MAPPING, END_MAPPING, ALIAS, SCALAR, PAIR};
    
    struct Event
    {
        EventType type;
        char* scalar;
        char* value;
    };
    
    Event event;
    
    Yam();
    ~Yam();
    
    void set_input(const unsigned char* input, int length);
    void set_input(Yam_Read_Handler* handler, void* data);
    bool open_input_file(const char* filename);
    void close_input();
    
    ParseResultEnum parse_next();
    
    bool set_output(Yam_Write_Handler* handler, void* data);
    bool open_output_file(const char* filename);
    void close_output();
    
    bool emit_scalar(const char* scalar);
    bool emit_pair(const char* key, const char* value);
    bool emit_begin_mapping();
    bool emit_end_mapping();
    bool emit_begin_sequence();
    bool emit_end_sequence();
    
    private:
    
    enum ContainerType {C_NONE, C_MAPPING, C_SEQUENCE};
    
    // Input
    yaml_parser_t _parser;
    yaml_event_t _event;
    yaml_event_t _peek_event;
    FILE* _infile;
    std::list<ContainerType> _input_containers;
    bool _failed_peek;
    void clear_event();
    
    // Output
    yaml_emitter_t _emitter;
    std::list<ContainerType> _output_containers;
    FILE* _outfile;
    
    
    
};


#endif
