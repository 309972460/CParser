#include "stdafx.h"
#include "Parser.h"
#include "Lexer.h"

/**
 * �ݹ��½�����������ο��ԣ�
 *     https://github.com/lotabout/write-a-C-interpreter
 * �������£�
 *     http://lotabout.me/2015/write-a-C-interpreter-0
 */

CParser::CParser(string_t str)
: lexer(str)
{
    init();
}

CParser::~CParser()
{
}

void CParser::init()
{
    program();
    gen.eval();
}

void CParser::next()
{
    lexer_t token;
    do
    {
        token = lexer.next();
    } while (token == l_newline || token == l_space || token == l_comment);
    assert(token != l_error);
#if 0
    if (token != l_end)
    {
        printf("[%04d:%03d] %-12s - %s\n", \
            lexer.get_last_line(), \
            lexer.get_last_column(), \
            LEX_STRING(lexer.get_type()).c_str(), \
            lexer.current().c_str());
    }
#endif
}

void CParser::program()
{
    next();
    while (!lexer.is_type(l_end))
    {
        global_declaration();
    }
}

// ���ʽ
void CParser::expression(operator_t level)
{
    // ���ʽ�ж������ͣ��� `(char) *a[10] = (int *) func(b > 0 ? 10 : 20);
    //
    // 1. unit_unary ::= unit | unit unary_op | unary_op unit
    // 2. expr ::= unit_unary (bin_op unit_unary ...)

    // unit_unary()
    {
        if (lexer.is_type(l_end)) // ��β
        {
            error("unexpected token EOF of expression");
            assert(0);
        }
        if (lexer.is_integer()) // ����
        {
            auto tmp = lexer.get_integer();
            match_number();

            // emit code
            gen.emit(IMM);
            gen.emit(tmp);
            expr_type = lexer.get_type();
        }
        else if (lexer.is_type(l_string)) // �ַ���
        {
            auto idx = gen.save_string(lexer.get_string());
#if 1
            printf("[%04d:%03d] String> %04X '%s'\n", lexer.get_line(), lexer.get_column(), idx, lexer.get_string().c_str());
#endif

            // emit code
            gen.emit(IMM);
            gen.emit(idx);
            gen.emit(LOAD);
            match_type(l_string);

            while (lexer.is_type(l_string))
            {
                idx = gen.save_string(lexer.get_string());
#if 0
                printf("[%04d:%03d] String> %04X '%s'\n", lexer.get_line(), lexer.get_column(), idx, lexer.get_string().c_str());
#endif
                match_type(l_string);
            }

            expr_type = l_ptr;
            ptr_level = 1;
        }
        else if (lexer.is_keyword(k_sizeof)) // sizeof
        {
            // ֧�� `sizeof(int)`, `sizeof(char)` and `sizeof(*...)`
            match_keyword(k_sizeof);
            match_operator(op_lparan);
            expr_type = l_int;
            ptr_level = 0;

            if (lexer.is_keyword(k_unsigned))
            {
                match_keyword(k_unsigned); // �з��Ż��޷��Ŵ�С��ͬ
            }

            auto size = lexer.get_sizeof();
            next();

            while (lexer.is_operator(op_times))
            {
                match_operator(op_times);
                if (expr_type != l_ptr)
                {
                    size = LEX_SIZEOF(ptr); // ָ���С
                    expr_type = l_ptr;
                }
            }

            match_operator(op_rparan);

            // emit code
            gen.emit(IMM);
            gen.emit(size);

            expr_type = l_int;
            ptr_level = 0;
        }
        else if (lexer.is_type(l_identifier)) // ����
        {
            // ���ֿ���
            // 1. function call ����������
            // 2. Enum variable ö��ֵ
            // 3. global/local variable ȫ��/�ֲ�������
            match_type(l_identifier);

            auto func_id = id; // ���浱ǰ�ı�����(��Ϊ����Ǻ������ã�id�ᱻ����)

            if (lexer.is_operator(op_lparan)) // ��������
            {
                // function call
                match_operator(op_lparan);

                // pass in arguments
                auto tmp = 0; // number of arguments
                while (!lexer.is_operator(op_rparan)) // ��������
                {
                    expression(op_assign);
                    gen.emit(PUSH);
                    tmp++;

                    if (lexer.is_operator(op_comma))
                    {
                        match_operator(op_comma);
                    }
                }
                match_operator(op_rparan);

                id = func_id;

                // emit code
                if (id->cls == Sys) // �ڽ�����
                {
                    // system functions
                    gen.emit(*id);
                }
                else if (id->cls == Fun) // ��ͨ����
                {
                    // function call
                    gen.emit(CALL);
                    gen.emit(*id);
                }
                else
                {
                    error("bad function call");
                }

                // ���ջ�ϲ���
                if (tmp > 0)
                {
                    gen.emit(ADJ);
                    gen.emit(tmp);
                }
                expr_type = id->type;
                ptr_level = id->ptr;
            }
            else if (id->cls == Num)
            {
                // enum variable
                gen.emit(IMM);
                gen.emit(*id);
                expr_type = l_int;
                ptr_level = 0;
            }
            else
            {
                // variable
                if (id->cls == Loc) // ���ر���
                {
                    gen.emit(LEA);
                    gen.emit(*id, ebp);
                }
                else if (id->cls == Glo) // ȫ�ֱ���
                {
                    gen.emit(IMM);
                    gen.emit(*id);
                    gen.emit(LOAD);
                }
                else
                {
                    error("undefined variable");
                }
                // emit code
                // ��ȡֵ��ax�Ĵ�����
                expr_type = id->type;
                ptr_level = id->ptr;
                gen.emitl(id->ptr > 0 ? l_int : expr_type);
            }
        }
        else if (lexer.is_operator(op_lparan)) // ǿ��ת��
        {
            // cast or parenthesis
            match_operator(op_lparan);
            if (lexer.is_type(l_keyword))
            {
                auto tmp = parse_type();
                auto ptr = 0;

                while (lexer.is_operator(op_times))
                {
                    match_operator(op_times);
                    ptr++;
                }
                match_operator(op_rparan);

                expression(op_plus_plus);
                expr_type = tmp;
                ptr_level = ptr;
            }
            else
            {
                // ��ͨ����Ƕ��
                expression(op_assign);
                match_operator(op_rparan);
            }
        }
        else if (lexer.is_operator(op_times)) // ������
        {
            // dereference *<addr>
            match_operator(op_times);
            expression(op_plus_plus);
            ptr_level--;

            gen.emitl(id->ptr > 0 ? l_int : expr_type);
        }
        else if (lexer.is_operator(op_bit_and)) // ȡ��ַ
        {
            // get the address of
            match_operator(op_bit_and);
            expression(op_plus_plus);
            if (gen.top() == LI || gen.top() == LC)
            {
                gen.pop();
            }
            else
            {
                error("bad address of");
            }

            ptr_level++;
        }
        else if (lexer.is_operator(op_logical_not))
        {
            // not
            match_operator(op_logical_not);
            expression(op_plus_plus);

            // emit code, use <expr> == 0
            gen.emit(PUSH);
            gen.emit(IMM);
            gen.emit(0);
            gen.emit(EQ);

            expr_type = l_int;
            ptr_level = 0;
        }
        else if (lexer.is_operator(op_bit_not))
        {
            // bitwise not
            match_operator(op_bit_not);
            expression(op_plus_plus);

            // emit code, use <expr> XOR -1
            gen.emit(PUSH);
            gen.emit(IMM);
            gen.emit(-1);
            gen.emit(XOR);

            expr_type = l_int;
            ptr_level = 0;
        }
        else if (lexer.is_operator(op_plus))
        {
            // +var, do nothing
            match_operator(op_plus);
            expression(op_plus_plus);

            expr_type = l_int;
            ptr_level = 0;
        }
        else if (lexer.is_operator(op_minus))
        {
            // -var
            match_operator(op_minus);

            if (lexer.is_integer())
            {
                gen.emit(IMM);
                gen.emit(lexer.get_integer());
                match_integer();
            }
            else
            {
                gen.emit(IMM);
                gen.emit(-1);
                gen.emit(PUSH);
                expression(op_plus_plus);
                gen.emit(MUL);
            }

            expr_type = l_int;
            ptr_level = 0;
        }
        else if (lexer.is_operator(op_plus_plus, op_minus_minus))
        {
            auto tmp = lexer.get_operator();
            match_type(l_operator);
            expression(op_plus_plus);
            if (gen.top() == LI)
            {
                gen.top(PUSH);
                gen.emit(LI);
            }
            else if (gen.top() == LC)
            {
                gen.top(PUSH);
                gen.emit(LC);
            }
            else
            {
                error("bad lvalue of pre-increment");
            }
            gen.emit(PUSH);
            gen.emit(IMM);
            gen.emit(ptr_level > 0 ? LEX_SIZEOF(ptr) : 1);
            gen.emit(tmp == op_plus_plus ? ADD : SUB);
            gen.emits(ptr_level > 0 ? l_int : expr_type);
        }
        else
        {
            error("bad expression");
        }
    }

    // ��Ԫ���ʽ�Լ���׺������
    {
        while (lexer.is_type(l_operator) && OPERATOR_PRED(lexer.get_operator()) <= OPERATOR_PRED(level)) // ���ȼ��ж�
        {
            auto tmp = expr_type;
            auto ptr = ptr_level;
            if (lexer.is_operator(op_rparan) || lexer.is_operator(op_rsquare) || lexer.is_operator(op_colon))
            {
                break;
            }
            if (lexer.is_operator(op_assign))
            {
                // var = expr;
                match_operator(op_assign);
                if (gen.top() == LI || gen.top() == LC)
                {
                    gen.top(PUSH); // save the lvalue's pointer
                }
                else
                {
                    error("bad lvalue in assignment");
                }
                expression(op_assign);
                expr_type = tmp;
                ptr_level = ptr;
                gen.emits(ptr_level > 0 ? l_int : expr_type);
            }
            else if (lexer.is_operator(op_query))
            {
                // expr ? a : b;
                match_operator(op_query);
                gen.emit(JZ);
                auto addr = gen.index();
                expression(op_assign);

                if (lexer.is_operator(op_colon))
                {
                    match_operator(op_colon);
                }
                else
                {
                    error("missing colon in conditional");
                }

                gen.emit(gen.index() + 3, addr);
                gen.emit(JMP);
                addr = gen.index();
                expression(op_query);
                gen.emit(gen.index() + 1, addr);
            }
#define MATCH_BINOP(op, inc, pred) \
    else if (lexer.is_operator(op)) { \
        match_operator(op); \
        gen.emit(PUSH); \
        expression(pred); \
        gen.emit(inc); \
        expr_type = l_int; \
        ptr_level = 0; \
    }

MATCH_BINOP(op_bit_or, OR, op_bit_xor)
MATCH_BINOP(op_bit_xor, XOR, op_bit_and)
MATCH_BINOP(op_bit_and, AND, op_equal)
MATCH_BINOP(op_equal, EQ, op_not_equal)
MATCH_BINOP(op_not_equal, NE, op_less_than)
MATCH_BINOP(op_less_than, LT, op_left_shift)
MATCH_BINOP(op_less_than_or_equal, LE, op_left_shift)
MATCH_BINOP(op_greater_than, GT, op_left_shift)
MATCH_BINOP(op_greater_than_or_equal, GE, op_left_shift)
MATCH_BINOP(op_left_shift, SHL, op_plus)
MATCH_BINOP(op_right_shift, SHR, op_plus)
MATCH_BINOP(op_times, MUL, op_plus_plus)
MATCH_BINOP(op_divide, DIV, op_plus_plus)
MATCH_BINOP(op_mod, MOD, op_plus_plus)
#undef MATCH_BINOP
            else if (lexer.is_operator(op_plus))
            {
                // add
                match_operator(op_plus);
                gen.emit(PUSH);
                expression(op_times);
                expr_type = l_int;
                ptr_level = ptr;
                expr_type = tmp;
                if (ptr > 0) {
                    gen.emit(PUSH);
                    gen.emit(IMM);
                    gen.emit(LEX_SIZEOF(ptr));
                    gen.emit(MUL);
                }
                gen.emit(ADD);
            }
            else if (lexer.is_operator(op_minus))
            {
                // sub
                match_operator(op_minus);
                gen.emit(PUSH);
                expression(op_times);
                expr_type = l_int;
                ptr_level = ptr;
                expr_type = tmp;
                if (ptr > 0) {
                    gen.emit(PUSH);
                    gen.emit(IMM);
                    gen.emit(LEX_SIZEOF(ptr));
                    gen.emit(MUL);
                }
                gen.emit(SUB);
            }
            else if (lexer.is_operator(op_logical_or))
            {
                // logic or
                match_operator(op_logical_or);
                gen.emit(JNZ);
                auto addr = gen.index();
                gen.emit(-1);
                expression(op_logical_and);
                gen.emit(gen.index(), addr);
                expr_type = l_int;
                ptr_level = 0;
            }
            else if (lexer.is_operator(op_logical_and))
            {
                // logic and
                match_operator(op_logical_and);
                gen.emit(JZ);
                auto addr = gen.index();
                gen.emit(-1);
                expression(op_bit_or);
                gen.emit(gen.index(), addr);
                expr_type = l_int;
                ptr_level = 0;
            }
            else if (lexer.is_operator(op_plus_plus, op_minus_minus))
            {
                auto tmp2 = lexer.get_operator();
                match_type(l_operator);
                // postfix inc(++) and dec(--)
                // we will increase the value to the variable and decrease it
                // on `ax` to get its original value.
                // ��������
                if (gen.top() == LI)
                {
                    gen.top(PUSH);
                    gen.emit(LI);
                }
                else if (gen.top() == LC)
                {
                    gen.top(PUSH);
                    gen.emit(LC);
                }
                else
                {
                    error("bad value in increment");
                }

                gen.emit(PUSH);
                gen.emit(IMM);
                gen.emit(ptr_level > 0 ? LEX_SIZEOF(ptr) : 1);
                gen.emit(tmp2 == op_plus_plus ? ADD : SUB);
                gen.emits(ptr_level > 0 ? l_int : expr_type);

                gen.emit(PUSH);
                gen.emit(IMM);
                gen.emit(ptr_level > 0 ? LEX_SIZEOF(ptr) : 1);
                gen.emit(tmp2 == op_plus_plus ? SUB : ADD);
            }
            else if (lexer.is_operator(op_lsquare))
            {
                // array access var[xx]
                match_operator(op_lsquare);
                gen.emit(PUSH);
                expression(op_assign);
                match_operator(op_rsquare);

                if (ptr > 0)
                {
                    // pointer, `not char *`
                    if (id->type != l_char)
                    {
                        gen.emit(PUSH);
                        gen.emit(IMM);
                        gen.emit(LEX_SIZEOF(int));
                        gen.emit(MUL);
                    }
                    gen.emit(ADD);
                    gen.emit(LI);
                    expr_type = tmp;
                    ptr_level = ptr - 1;
                }
                else
                {
                    error("pointer type expected");
                }
            }
            else
            {
                error("compiler error, token = " + lexer.current());
            }
        }
    }
}

// �������
void CParser::statement()
{
    // there are 8 kinds of statements here:
    // 1. if (...) <statement> [else <statement>]
    // 2. while (...) <statement>
    // 3. { <statement> }
    // 4. return xxx;
    // 5. <empty statement>;
    // 6. expression; (expression end with semicolon)

    if (lexer.is_keyword(k_if)) // if�ж�
    {
        // if (...) <statement> [else <statement>]
        //
        //   if (...)           <cond>
        //                      JZ a
        //     <statement>      <statement>
        //   else:              JMP b
        // a:
        //     <statement>      <statement>
        // b:                   b:
        //
        //
        match_keyword(k_if);
        match_operator(op_lparan);
        expression(op_assign);  // if�жϵ�����
        match_operator(op_rparan);

        // emit code for if
        gen.emit(JZ);
        auto b = gen.index(); // ��֧�жϣ�����Ҫ��д��b(��if��β)
        gen.emit(-1);

        statement();  // ����ifִ����
        if (lexer.is_keyword(k_else)) { // ����else
            match_keyword(k_else);

            // emit code for JMP B
            gen.emit(gen.index() + 2, b);
            gen.emit(JMP);
            b = gen.index();
            gen.emit(-1);

            statement();
        }

        gen.emit(gen.index(), b);
    }
    else if (lexer.is_keyword(k_while)) // whileѭ��
    {
        //
        // a:                     a:
        //    while (<cond>)        <cond>
        //                          JZ b
        //     <statement>          <statement>
        //                          JMP a
        // b:                     b:
        match_keyword(k_while);

        auto a = gen.index();

        match_operator(op_lparan);
        expression(op_assign); // ����
        match_operator(op_rparan);

        gen.emit(JZ);
        auto b = gen.index(); // �˳��ĵط�
        gen.emit(-1);

        statement();

        gen.emit(JMP);
        gen.emit(a); // �ظ�ѭ��
        gen.emit(gen.index(), b);
    }
    else if (lexer.is_operator(op_lbrace)) // ���
    {
        // { <statement> ... }
        match_operator(op_lbrace);

        while (!lexer.is_operator(op_rbrace))
        {
            statement();
        }

        match_operator(op_rbrace);
    }
    else if (lexer.is_keyword(k_return)) // ����
    {
        // return [expression];
        match_keyword(k_return);

        if (!lexer.is_operator(op_semi))
        {
            expression(op_assign);
        }

        match_operator(op_semi);

        // emit code for return
        gen.emit(LEV);
    }
    else if (lexer.is_operator(op_semi)) // �����
    {
        // empty statement
        match_operator(op_semi);
    }
    else // ���ʽ
    {
        // a = b; or function_call();
        expression(op_assign);
        match_operator(op_semi);
    }
}

// ö������
void CParser::enum_declaration()
{
    // parse enum [id] { a = 1, b = 3, ...}
    int i = 0;
    while (!lexer.is_operator(op_rbrace))
    {
        if (!lexer.is_type(l_identifier))
        {
            error("bad enum identifier " + lexer.current());
        }
        match_type(l_identifier);
        if (lexer.is_operator(op_assign)) // ��ֵ
        {
            // like { a = 10 }
            next();
            if (!lexer.is_integer())
            {
                error("bad enum initializer");
            }
            i = lexer.get_integer();
            next();
        }

        // ����ֵ��������
        id->cls = Num;
        id->type = l_int;
        id->value._int = i++;

        if (lexer.is_operator(op_comma))
        {
            next();
        }
    }
}

// ��������
void CParser::function_parameter()
{
    auto params = 0;
    while (!lexer.is_operator(op_rparan)) // �жϲ��������Ž�β
    {
        // int name, ...
        auto type = parse_type(); // ��������
        auto ptr = 0;

        // pointer type
        while (lexer.is_operator(op_times)) // ָ��
        {
            match_operator(op_times);
            ptr++;
        }

        // parameter name
        if (!lexer.is_type(l_identifier))
        {
            error("bad parameter declaration");
        }

        match_type(l_identifier);
        if (id->cls == Loc) // �����������ͻ
        {
            error("duplicate parameter declaration");
        }

        // ���汾�ر���
        // ����ΪʲôҪ������ط�����֮ǰ��ֵ������Ϊ��������(�����Ż���)������
        // ����һ��������ʱ��ȫ�ֱ�����Ҫ���棬�˳�������ʱ�ָ�
        id->_cls = id->cls; id->cls = Loc;
        id->_type = id->type; id->type = type;
        id->_value._int = id->value._int; id->value._int = params; // ������ջ�ϵ�ַ
        id->_ptr = id->ptr; id->ptr = ptr;

        params += 4;

        if (lexer.is_operator(op_comma))
        {
            match_operator(op_comma);
        }
    }
    ebp = params + 4;
}

// ������
void CParser::function_body()
{
    // type func_name (...) {...}
    //                   -->|   |<--

    // ...
    {
        // 1. local declarations
        // 2. statements
        // }

        auto pos_local = ebp; // ������ջ�ϵ�ַ

        while (lexer.is_basetype())
        {
            // �����������
            base_type = parse_type();

            while (!lexer.is_operator(op_semi)) // �ж�������
            {
                auto ptr = 0;
                auto type = base_type;
                while (lexer.is_operator(op_times)) // ����ָ��
                {
                    match_operator(op_times);
                    ptr++;
                }

                if (!lexer.is_type(l_identifier))
                {
                    // invalid declaration
                    error("bad local declaration");
                }
                match_type(l_identifier);
                if (id->cls == Loc) // �����ظ�����
                {
                    // identifier exists
                    error("duplicate local declaration");
                }

                // ���汾�ر���
                // ����ΪʲôҪ������ط�����֮ǰ��ֵ������Ϊ��������(�����Ż���)������
                // ����һ��������ʱ��ȫ�ֱ�����Ҫ���棬�˳�������ʱ�ָ�
                id->_cls = id->cls; id->cls = Loc;
                id->_type = id->type; id->type = type;
                id->_value._int = id->value._int; id->value._int = pos_local;  // ������ջ�ϵ�ַ
                id->_ptr = id->ptr; id->ptr = ptr;

                pos_local += 4;

                if (lexer.is_operator(op_comma))
                {
                    match_operator(op_comma);
                }
            }
            match_operator(op_semi);
        }

        // save the stack size for local variables
        gen.emit(ENT);
        gen.emit(pos_local - ebp);

        // statements
        while (!lexer.is_operator(op_rbrace))
        {
            statement();
        }

        // emit code for leaving the sub function
        gen.emit(LEV);
    }
}

// ��������
void CParser::function_declaration()
{
    // type func_name (...) {...}
    //               | this part

    match_operator(op_lparan);
    function_parameter();
    match_operator(op_rparan);
    match_operator(op_lbrace);
    function_body();
    // match('}'); ���ﲻ������������Ϊ���ϲ㺯���жϽ�β

    gen.unwind();
}

// �����������(ȫ�ֻ�������)
// int [*]id [; | (...) {...}]
void CParser::global_declaration()
{
    base_type = l_int;

    // ����enumö��
    if (lexer.is_keyword(k_enum))
    {
        // enum [id] { a = 10, b = 20, ... }
        match_keyword(k_enum);
        if (!lexer.is_operator(op_lbrace))
        {
            match_type(l_identifier); // ʡ����[id]ö����
        }
        if (lexer.is_operator(op_lbrace))
        {
            // ����ö����
            match_operator(op_lbrace);
            enum_declaration(); // ö�ٵı����������֣��� a = 10, b = 20, ...
            match_operator(op_rbrace);
        }

        match_operator(op_semi);
        return;
    }

    // �����������ͣ�����������ʱ������
    base_type = parse_type();
    if (base_type == l_none)
        base_type = l_int;

    // �����ŷָ��ı�������
    while (!lexer.is_operator(op_semi, op_rbrace))
    {
        auto type = base_type; // ��������������Ϊ����
        auto ptr = 0;
        // ����ָ��, ��`int ****x;`
        while (lexer.is_operator(op_times))
        {
            match_operator(op_times);
            ptr++;
        }

        if (!lexer.is_type(l_identifier)) // �����ڱ������򱨴�
        {
            // invalid declaration
            error("bad global declaration");
        }
        match_type(l_identifier);
        if (id->cls) // �������Ѿ����������ظ���������
        {
            // identifier exists
            error("duplicate global declaration");
        }
        id->type = type;
        id->ptr = ptr;

        if (lexer.is_operator(op_lparan)) // ����������Ӧ�ж��Ǻ�������
        {
            id->cls = Fun;
            id->value._int = gen.index(); // ��¼������ַ
#if 1
            printf("[%04d:%03d] Function> %04X '%s'\n", lexer.get_line(), lexer.get_column(), id->value._int * 4, id->name.c_str());
#endif
            function_declaration();
#if 1
            printf("[%04d:%03d] Function> %04X '%s'\n", lexer.get_line(), lexer.get_column(), id->value._int * 4, id->name.c_str());
#endif
        }
        else
        {
            // �����������
            id->cls = Glo; // ȫ�ֱ���
            id->value._int = gen.get_data(); // ��¼������ַ
#if 1
            printf("[%04d:%03d] Global> %04X '%s'\n", lexer.get_line(), lexer.get_column(), id->value._int, id->name.c_str());
#endif
        }

        if (lexer.is_operator(op_comma))
        {
            match_operator(op_comma);
        }
    }
    next();
}

void CParser::match_keyword(keyword_t type)
{
    assert(lexer.is_keyword(type));
    next();
}

void CParser::match_operator(operator_t type)
{
    assert(lexer.is_operator(type));
    next();
}

void CParser::match_type(lexer_t type)
{
    assert(lexer.is_type(type));
    if (type == l_identifier)
        save_identifier();
    next();
}

void CParser::match_number()
{
    assert(lexer.is_number());
    next();
}

void CParser::match_integer()
{
    assert(lexer.is_integer());
    next();
}

// �����������
// ��char,short,int,long�Լ���Ӧ���޷�������(�޷�����ʱ��֧��)
// �Լ�float��double(��ʱ��֧��)
lexer_t CParser::parse_type()
{
    auto type = l_int;
    if (lexer.is_type(l_keyword))
    {
        auto _unsigned = false;
        if (lexer.is_keyword(k_unsigned)) // �ж��Ƿ����unsignedǰ׺
        {
            _unsigned = true;
            match_keyword(k_unsigned);
        }
        type = lexer.get_typeof(_unsigned); // ����keyword�õ�lexer_t
        match_type(l_keyword);
    }
    return type;
}

// ����ո�ʶ��ı�����
void CParser::save_identifier()
{
    id = gen.add_sym(lexer.get_identifier());
    if (id->ptr == 0)
        id->ptr = ptr_level;
}

void CParser::error(string_t info)
{
    printf("[%04d:%03d] ERROR: %s\n", lexer.get_line(), lexer.get_column(), info.c_str());
    assert(0);
}
