import json
import sys

def indent_str(level):
    """주어진 들여쓰기(level, 네 칸 기준)에 따른 공백 문자열 반환"""
    return "    " * level

def get_declname(node):
    """
    노드(또는 노드 트리) 내에 'declname' 필드가 있다면 그 값을 반환합니다.
    없으면 None을 반환.
    """
    if isinstance(node, dict):
        if 'declname' in node and node['declname']:
            return node['declname']
        for key, value in node.items():
            result = get_declname(value)
            if result:
                return result
    elif isinstance(node, list):
        for item in node:
            result = get_declname(item)
            if result:
                return result
    return None

def generate_code(node, level=0, in_param=False):
    """AST 노드를 재귀적으로 순회하여 C 코드 문자열로 변환합니다.
    in_param이 True이면 파라미터 목록 형식으로 처리(세미콜론 제거 등)"""
    if isinstance(node, dict):
        nodetype = node.get('_nodetype', '')
        # 각 노드별 분기 처리
        if nodetype == 'FileAST':
            code_lines = [generate_code(ext, level) for ext in node.get('ext', [])]
            return "\n\n".join(code_lines)
        
        elif nodetype == 'Decl':
            type_node = node.get('type')
            # 함수 선언/정의 처리
            if type_node and type_node.get('_nodetype') == 'FuncDecl':
                ret_type = generate_code(type_node.get('type'), level, in_param)
                fn_name = node.get('name', '')
                args_node = type_node.get('args')
                if args_node:
                    params = args_node.get('params', [])
                    params_code = ", ".join(generate_code(param, level, in_param=True) for param in params)
                    if params_code == "":
                        params_code = "void"
                else:
                    params_code = "void"
                # ret_type가 이미 함수 이름을 포함하고 있는지 확인
                tokens = ret_type.split()
                if tokens and tokens[-1] == fn_name:
                    code_line = ret_type + "(" + params_code + ")"
                else:
                    code_line = ret_type + " " + fn_name + "(" + params_code + ")"
                init = node.get('init')
                if init is not None:
                    code_line += " = " + generate_code(init, level)
                return (indent_str(level) + code_line) if not in_param else code_line
            else:
                type_code = generate_code(type_node, level, in_param)
                name = node.get('name', '')
                init = node.get('init')
                code_line = ""
                if type_code:
                    code_line += type_code
                declname = get_declname(type_node)
                if name and (declname is None or declname != name):
                    code_line += " " + name
                if init is not None:
                    code_line += " = " + generate_code(init, level)
                # 파라미터일 때는 세미콜론 없이 반환
                return (indent_str(level) + code_line) if not in_param else code_line
        
        elif nodetype == 'FuncDef':
            decl_code = generate_code(node.get('decl'), level)
            body_code = generate_code(node.get('body'), level)
            if decl_code.endswith(";"):
                decl_code = decl_code[:-1]
            return decl_code + " " + body_code
        
        elif nodetype == 'FuncDecl':
            return ""
        
        elif nodetype == 'TypeDecl':
            inner = generate_code(node.get('type'), level, in_param)
            declname = node.get('declname')
            if declname:
                return inner + " " + declname
            return inner
        
        elif nodetype == 'IdentifierType':
            names = node.get('names', [])
            return " ".join(names)
        
        elif nodetype == 'PtrDecl':
            # C에서는 보통 "char *old" 형태이므로, PtrDecl를 처리할 때 뒤에 붙인 후 이름을 붙입니다.
            return generate_code(node.get('type'), level, in_param) + " *"
        
        elif nodetype == 'Typename':
            return generate_code(node.get('type'), level, in_param)
        
        elif nodetype == 'Compound':
            lines = [indent_str(level) + "{"]
            block_items = node.get('block_items') or []
            for item in block_items:
                lines.append(generate_code(item, level + 1))
            lines.append(indent_str(level) + "}")
            return "\n".join(lines)
        
        elif nodetype == 'Return':
            expr = node.get('expr')
            if expr:
                return indent_str(level) + "return " + generate_code(expr, level) + ";"
            else:
                return indent_str(level) + "return;"
        
        elif nodetype == 'FuncCall':
            name_code = generate_code(node.get('name'), level, in_param)
            args = ""
            if node.get('args'):
                exprs = node['args'].get('exprs', [])
                args = ", ".join(generate_code(expr, level) for expr in exprs)
            return name_code + "(" + args + ")"
        
        elif nodetype == 'Assignment':
            lvalue = generate_code(node.get('lvalue'), level, in_param)
            rvalue = generate_code(node.get('rvalue'), level, in_param)
            op = node.get('op', '=')
            return indent_str(level) + lvalue + " " + op + " " + rvalue + ";"
        
        elif nodetype == 'BinaryOp':
            left = generate_code(node.get('left'), level, in_param)
            right = generate_code(node.get('right'), level, in_param)
            op = node.get('op', '')
            return "(" + left + " " + op + " " + right + ")"
        
        elif nodetype == 'ID':
            return node.get('name', '')
        
        elif nodetype == 'Constant':
            return node.get('value', '')
        
        elif nodetype == 'If':
            cond = generate_code(node.get('cond'), level, in_param)
            iftrue = generate_code(node.get('iftrue'), level, in_param)
            code = indent_str(level) + "if (" + cond + ") " + iftrue
            if node.get('iffalse'):
                else_code = generate_code(node.get('iffalse'), level, in_param)
                code += " else " + else_code
            return code
        
        elif nodetype == 'While':
            cond = generate_code(node.get('cond'), level, in_param)
            stmt = generate_code(node.get('stmt'), level, in_param)
            return indent_str(level) + "while (" + cond + ") " + stmt
        
        elif nodetype == 'ArrayRef':
            arr = generate_code(node.get('name'), level, in_param)
            sub = generate_code(node.get('subscript'), level, in_param)
            return arr + "[" + sub + "]"
        
        # 처리하지 않은 노드의 자식들을 순회
        code = ""
        for key, value in node.items():
            if isinstance(value, dict):
                code += generate_code(value, level, in_param)
            elif isinstance(value, list):
                for item in value:
                    code += generate_code(item, level, in_param)
        return code
    elif isinstance(node, list):
        return "\n".join(generate_code(item, level, in_param) for item in node)
    else:
        return str(node)

def main():
    if len(sys.argv) < 2:
        print("Usage: python ast_to_c_pretty.py ast.json")
        return

    filename = sys.argv[1]
    try:
        with open(filename, 'r', encoding='utf-8') as f:
            ast = json.load(f)
    except Exception as e:
        print("Error reading AST file:", e)
        return

    code = generate_code(ast)
    print(code)

if __name__ == "__main__":
    main()
