/*
Yam v0.1.0 by Jonathan Dearborn
Yam is a C++ wrapper for the basic functionality of libyaml.

The Yam source code is released under the Boost Software License v1.0.
See LICENSE for details.
*/

#include "yam.h"


Yam::Yam()
    : _infile(NULL), _failed_peek(false), _outfile(NULL)
{
    yaml_parser_initialize(&_parser);
    _event.type = YAML_NO_EVENT;
    
    event.type = Yam::NONE;
    event.scalar = NULL;
    event.value = NULL;
    
    yaml_emitter_initialize(&_emitter);
    yaml_emitter_set_encoding(&_emitter, YAML_UTF8_ENCODING);
}

Yam::~Yam()
{
    close_output();
    close_input();
    
    yaml_event_delete(&_event);
}

void Yam::clear_event()
{
    if(_failed_peek)
    {
        // Pass the peeked event back so it gets deleted later
        _failed_peek = false;
        _event = _peek_event;
    }
    
    free(event.scalar);
    event.scalar = NULL;
    
    free(event.value);
    event.value = NULL;
}

void Yam::set_input(const unsigned char* input, int length)
{
    close_input();
    
    yaml_parser_initialize(&_parser);
    yaml_parser_set_input_string(&_parser, input, length);
}

void Yam::set_input(Yam_Read_Handler* handler, void* data)
{
    yaml_parser_set_input(&_parser, handler, data);
}

bool Yam::open_input_file(const char* filename)
{
    close_input();
    
    _infile = fopen(filename, "r");
    if(_infile == NULL)
        return false;
    
    yaml_parser_initialize(&_parser);
    yaml_parser_set_input_file(&_parser, _infile);
    
    return true;
}

void Yam::close_input()
{
    if(&_parser.read_handler != NULL)
        yaml_parser_delete(&_parser);
    
    clear_event();
    
    if(_infile)
    {
        fclose(_infile);
        _infile = NULL;
    }
}

bool Yam::set_output(Yam_Write_Handler* handler, void* data)
{
    close_output();
    
    if(!yaml_emitter_initialize(&_emitter))
        return false;
    yaml_emitter_set_encoding(&_emitter, YAML_UTF8_ENCODING);
    
    yaml_emitter_set_output(&_emitter, handler, data);
    if(!yaml_emitter_open(&_emitter))
        return false;
    
    yaml_event_t output_event;
        
    if(!yaml_document_start_event_initialize(&output_event, NULL, NULL, NULL, 0))
        return false;
    
    yaml_emitter_emit(&_emitter, &output_event);
    return true;
}

bool Yam::open_output_file(const char* filename)
{
    close_output();
    
    _outfile = fopen(filename, "w");
    if(_outfile == NULL)
        return false;
    
    if(!yaml_emitter_initialize(&_emitter))
        return false;
    yaml_emitter_set_encoding(&_emitter, YAML_UTF8_ENCODING);
    
    yaml_emitter_set_output_file(&_emitter, _outfile);
    if(!yaml_emitter_open(&_emitter))
        return false;
    
    yaml_event_t output_event;
        
    if(!yaml_document_start_event_initialize(&output_event, NULL, NULL, NULL, 0))
        return false;
    
    yaml_emitter_emit(&_emitter, &output_event);
    return true;
}

void Yam::close_output()
{
    if(_emitter.write_handler != NULL)
    {
        while(_output_containers.size() > 0)
        {
            switch(_output_containers.back())
            {
            case C_MAPPING:
                emit_end_mapping();
                break;
            case C_SEQUENCE:
                emit_end_sequence();
                break;
            default:
                break;
            }
            
            _output_containers.pop_back();
        }
        
        yaml_event_t output_event;
        if (yaml_document_end_event_initialize(&output_event, 1))
        {
            yaml_emitter_emit(&_emitter, &output_event);
        }
        
        yaml_emitter_flush(&_emitter);
        // Destroy the emitter
        yaml_emitter_close(&_emitter);
        yaml_emitter_delete(&_emitter);
    }
    
    if(_outfile)
    {
        fclose(_outfile);
        _outfile = NULL;
    }
}

Yam::ParseResultEnum Yam::parse_next()
{
    if(_failed_peek)
    {
        _failed_peek = false;
        _event = _peek_event;
    }
    else
    {
        yaml_event_delete(&_event);
        
        if(!yaml_parser_parse(&_parser, &_event))
            return ERROR;
    }
    
    if(_event.type == YAML_STREAM_END_EVENT)
        return DONE;
    
    event.type = NONE;
    switch(_event.type)
    {
        // A list
        case YAML_SEQUENCE_START_EVENT:
            event.type = BEGIN_SEQUENCE;
            _input_containers.push_back(C_SEQUENCE);
            break;
        case YAML_SEQUENCE_END_EVENT:
            event.type = END_SEQUENCE;
            if(_input_containers.size() == 0 || _input_containers.back() != C_SEQUENCE)
                return ERROR;
            
            _input_containers.pop_back();
            break;
        // A nested group
        case YAML_MAPPING_START_EVENT:
            event.type = BEGIN_MAPPING;
            _input_containers.push_back(C_MAPPING);
            break;
        case YAML_MAPPING_END_EVENT:
            event.type = END_MAPPING;
            if(_input_containers.size() == 0 || _input_containers.back() != C_MAPPING)
                return ERROR;
            
            _input_containers.pop_back();
            break;
        // Data
        case YAML_ALIAS_EVENT:
            // TODO: Make aliases work
            event.type = ALIAS;
            break;
        case YAML_SCALAR_EVENT:
                event.type = SCALAR;
                free(event.scalar);
                event.scalar = strdup((char*)_event.data.scalar.value);
                
                if(_input_containers.size() > 0 && _input_containers.back() == C_MAPPING)
                {
                    // Now let's peek at the next event to see if it is a simple pair
                    yaml_event_delete(&_event);
                    
                    if(!yaml_parser_parse(&_parser, &_event))
                        return ERROR;
                    
                    if(_event.type == YAML_SCALAR_EVENT)
                    {
                        // A pair
                        event.type = PAIR;
                        free(event.value);
                        event.value = strdup((char*)_event.data.scalar.value);
                    }
                    else
                    {
                        // Not a pair, handle this event next time
                        _failed_peek = true;
                        _peek_event = _event;  // Save the event
                        memset(&_event, 0, sizeof(yaml_event_t));  // Clear the event buffer
                    }
                }
            return OK;
            break;
        default:
            break;
    }
    
    return OK;
}

bool Yam::emit_scalar(const char* scalar)
{
    if(_output_containers.size() == 0)
    {
        emit_begin_sequence();
        _output_containers.push_back(C_SEQUENCE);
    }
    
    yaml_event_t output_event;
    if(yaml_scalar_event_initialize(&output_event,
                            NULL, NULL, (yaml_char_t*)scalar, -1,
                            1, 1, YAML_ANY_SCALAR_STYLE))
    {
        return yaml_emitter_emit(&_emitter, &output_event);
    }
    return false;
}


bool Yam::emit_pair(const char* key, const char* value)
{
    if(_output_containers.size() == 0)
    {
        emit_begin_mapping();
        _output_containers.push_back(C_MAPPING);
    }
    
    bool auto_contained = false;
    if(_output_containers.back() != C_MAPPING)
    {
        emit_begin_mapping();
        auto_contained = true;
    }
    
    yaml_event_t output_event;
    if(yaml_scalar_event_initialize(&output_event,
                            NULL, NULL, (yaml_char_t*)key, -1,
                            1, 1, YAML_ANY_SCALAR_STYLE))
    {
        if(!yaml_emitter_emit(&_emitter, &output_event))
            return false;
    }
    else
        return false;
    
    if(yaml_scalar_event_initialize(&output_event,
                            NULL, NULL, (yaml_char_t*)value, -1,
                            1, 1, YAML_ANY_SCALAR_STYLE))
    {
        yaml_emitter_emit(&_emitter, &output_event);
        
        if(auto_contained)
            emit_end_mapping();
    }
    return false;
}

bool Yam::emit_begin_mapping()
{
    _output_containers.push_back(C_MAPPING);
    
    yaml_event_t output_event;
    if(yaml_mapping_start_event_initialize(&output_event,
                            NULL, NULL, 1, YAML_ANY_MAPPING_STYLE))
    {
        return yaml_emitter_emit(&_emitter, &output_event);
    }
    return false;
}

bool Yam::emit_end_mapping()
{
    if(_output_containers.size() == 0 || _output_containers.back() != C_MAPPING)
        return false;
    
    _output_containers.pop_back();
    
    yaml_event_t output_event;
    if(yaml_mapping_end_event_initialize(&output_event))
    {
        return yaml_emitter_emit(&_emitter, &output_event);
    }
    return false;
}

bool Yam::emit_begin_sequence()
{
    _output_containers.push_back(C_SEQUENCE);
    
    yaml_event_t output_event;
    if(yaml_sequence_start_event_initialize(&output_event,
                            NULL, NULL, 1, YAML_ANY_SEQUENCE_STYLE))
    {
        return yaml_emitter_emit(&_emitter, &output_event);
    }
    return false;
}

bool Yam::emit_end_sequence()
{
    if(_output_containers.size() == 0 || _output_containers.back() != C_SEQUENCE)
        return false;
    
    _output_containers.pop_back();
    
    yaml_event_t output_event;
    if(yaml_sequence_end_event_initialize(&output_event))
    {
        return yaml_emitter_emit(&_emitter, &output_event);
    }
    return false;
}

