//
// $Id$
//

// ============================================================================
//
// = LIBRARY
//    TAO IDL
//
// = FILENAME
//    invoke_cs.h
//
// = DESCRIPTION
//    Visitors for generation of code for Arguments for passing the arguments
//    to the CDR << and >> operators
//
// = AUTHOR
//    Aniruddha Gokhale
//
// ============================================================================

#ifndef _BE_VISITOR_ARGUMENT_INVOKE_CS_H_
#define _BE_VISITOR_ARGUMENT_INVOKE_CS_H_

// ************************************************************
// class be_visitor_args_invoke_cs
// ************************************************************

class be_visitor_args_invoke_cs : public be_visitor_args
{
  //
  // = TITLE
  //   be_visitor_args_invoke_cs
  //
  // = DESCRIPTION
  //   Code to be generated when making the invocation
  //
public:
  be_visitor_args_invoke_cs (be_visitor_context *ctx);
  // constructor

  virtual ~be_visitor_args_invoke_cs (void);
  // destructor

  virtual int visit_argument (be_argument *node);
  // visit the argument node

  // =visit all the nodes that can be the types for the argument

  virtual int visit_array (be_array *node);
  // visit array type

  virtual int visit_enum (be_enum *node);
  // visit the enum node

  virtual int visit_interface (be_interface *node);
  // visit interface

  virtual int visit_interface_fwd (be_interface_fwd *node);
  // visit interface forward

  virtual int visit_valuetype (be_valuetype *node);
  // visit valuetype

  virtual int visit_valuetype_fwd (be_valuetype_fwd *node);
  // visit valuetype forward

  virtual int visit_predefined_type (be_predefined_type *node);
  // visit predefined type

  virtual int visit_sequence (be_sequence *node);
  // visit sequence type

  virtual int visit_string (be_string *node);
  // visit string type

  virtual int visit_structure (be_structure *node);
  // visit structure type

  virtual int visit_union (be_union *node);
  // visit union type

  virtual int visit_typedef (be_typedef *node);
  // visit the typedef type

};

#endif /* _BE_VISITOR_ARGUMENT_INVOKE_CS_H_ */
