/*
 * analyzer.c
 *
 * AST(JSON) 파일(ast.json)을 fopen()으로 읽어 들인 후, 아래 정보를 추출합니다.
 * 1. 전체 함수 개수 (함수 선언 및 정의 모두)
 * 2. 각 함수의 리턴 타입 추출
 * 3. 각 함수의 파라미터 (타입과 변수명) 추출
 * 4. (정의된 함수의 경우) 함수 본문 내 if 조건문의 개수 추출
 *
 * 참고: JSON 파싱은 제공된 json_c.c 라이브러리(헤더 포함)를 사용하며,
 *       json_create() 함수를 이용해 문자열을 JSON 객체로 변환합니다.
 *
 * 컴파일 예시:
 *   gcc analyzer.c json_c.c -o analyzer
 */

#include <stdio.h>
#include <memory.h>
#include "json_c.c"
#include <string.h>

#define MAX_BUF 1024

// --- 재귀적으로 AST를 순회하여 if 노드("_nodetype"가 "If") 개수를 셉니다 ---
int count_if_nodes(json_value node)
{
    int count = 0;
    // 현재 노드가 객체인 경우
    if (node.type == JSON_OBJECT && node.value != NULL)
    {
        json_object *obj = (json_object *)node.value;
        for (int i = 0; i <= obj->last_index; i++)
        {
            if (obj->keys[i] != NULL && strcmp(obj->keys[i], "_nodetype") == 0)
            {
                json_value nodetype_val = obj->values[i];
                if (nodetype_val.type == JSON_STRING)
                {
                    char *nodetype = json_to_string(nodetype_val);
                    if (strcmp(nodetype, "If") == 0)
                        count++;
                }
            }
            count += count_if_nodes(obj->values[i]);
        }
    }
    // 현재 노드가 배열인 경우
    else if (node.type == JSON_ARRAY && node.value != NULL)
    {
        json_array *arr = (json_array *)node.value;
        for (int i = 0; i <= arr->last_index; i++)
        {
            count += count_if_nodes(arr->values[i]);
        }
    }
    return count;
}

// --- JSON 객체에서 문자열 값 추출 (타입이 JSON_STRING일 경우) ---
char *get_json_string(json_value node)
{
    if (node.type == JSON_STRING && node.value != NULL)
        return (char *)node.value;
    return NULL;
}

/*
 * extract_type: 재귀적으로 AST 노드의 타입 정보를 추출하여 문자열로 반환.
 * 처리 방식:
 *   - IdentifierType: names 배열의 첫 번째 원소 반환
 *   - TypeDecl, Typename: 내부 "type" 필드를 재귀 호출
 *   - PtrDecl: 내부 "type"을 재귀 호출한 후, 앞에 "*"를 붙여 새 문자열을 반환 (동적 할당됨)
 *   - FuncDecl: 내부 "type"을 재귀 호출
 * 만약 올바른 타입 정보를 찾지 못하면 "unknown"을 반환합니다.
 */
char *extract_type(json_value node)
{
    if (node.type != JSON_OBJECT || node.value == NULL)
        return "unknown";

    char *nt = get_json_string(json_get(node, "_nodetype"));
    if (!nt)
        return "unknown";

    if (strcmp(nt, "IdentifierType") == 0)
    {
        json_value names = json_get(node, "names");
        if (names.type == JSON_ARRAY && names.value != NULL)
        {
            json_array *names_arr = (json_array *)names.value;
            if (names_arr->last_index >= 0)
            {
                char *res = get_json_string(names_arr->values[0]);
                return res ? res : "unknown";
            }
        }
        return "unknown";
    }
    else if (strcmp(nt, "TypeDecl") == 0 || strcmp(nt, "Typename") == 0)
    {
        return extract_type(json_get(node, "type"));
    }
    else if (strcmp(nt, "PtrDecl") == 0)
    {
        char *inner = extract_type(json_get(node, "type"));
        size_t len = strlen(inner);
        char *result = malloc(len + 2); // 1 for '*' + 1 for '\0'
        if (result)
            sprintf(result, "*%s", inner);
        return result;
    }
    else if (strcmp(nt, "FuncDecl") == 0)
    {
        return extract_type(json_get(node, "type"));
    }
    else
    {
        return "unknown";
    }
}

// --- 함수의 리턴타입 추출 (기존 extract_return_type()를 extract_type()으로 변경) ---
char *extract_return_type(json_value decl_type)
{
    return extract_type(decl_type);
}

// --- 함수의 파라미터 정보를 추출합니다 ---
// 함수 선언의 "type" 필드 내 "args" 항목의 "params" 배열을 읽음.
// 각 파라미터는 { "_nodetype": "Typename", type: { ... } , name: ... } 형태입니다.
void extract_params(json_value args_val, char *buf, size_t bufsize)
{
    buf[0] = '\0';
    if (args_val.type != JSON_OBJECT)
    {
        strncat(buf, "None", bufsize - strlen(buf) - 1);
        return;
    }
    json_value params_val = json_get(args_val, "params");
    if (params_val.type != JSON_ARRAY)
    {
        strncat(buf, "None", bufsize - strlen(buf) - 1);
        return;
    }
    json_array *params_arr = (json_array *)params_val.value;
    for (int i = 0; i <= params_arr->last_index; i++)
    {
        json_value param = params_arr->values[i];
        // 파라미터 이름 추출
        json_value name_val = json_get(param, "name");
        char *pname = get_json_string(name_val);
        if (!pname)
            pname = "anonymous";

        // 파라미터 타입 추출: 파라미터 노드의 "type" 필드를 extract_type()으로 처리
        char *ptype = extract_type(json_get(param, "type"));
        if (!ptype)
            ptype = "unknown";

        char param_info[128];
        snprintf(param_info, sizeof(param_info), "    %s %s\n", ptype, pname);
        strncat(buf, param_info, bufsize - strlen(buf) - 1);

        // 만약 ptype가 PtrDecl 처리로 동적할당되었다면 free() (문자열의 첫 문자가 '*'이면)
        if (ptype[0] == '*')
            free(ptype);
    }
}

// --- 함수 노드를 분석하여 함수명, 리턴타입, 파라미터 정보, if 조건문 개수를 출력합니다 ---
// 함수 노드는 두 가지 유형: 함수 정의(FuncDef)와 함수 선언(Decl) 중 내부 type._nodetype가 FuncDecl인 경우.
void process_function(json_value func_node)
{
    json_value decl;
    char *nodetype = get_json_string(json_get(func_node, "_nodetype"));
    if (nodetype && strcmp(nodetype, "FuncDef") == 0)
        decl = json_get(func_node, "decl"); // 함수 정의의 경우
    else
        decl = func_node; // 함수 선언의 경우

    // 함수 이름 추출
    json_value name_val = json_get(decl, "name");
    char *func_name = get_json_string(name_val);
    if (!func_name)
        func_name = "unknown";

    // 함수 리턴 타입 추출 (decl.type 내부)
    json_value type_val = json_get(decl, "type");
    char *return_type = extract_return_type(type_val);

    // 함수 파라미터 추출 (decl.type.args)
    json_value args_val = json_get(type_val, "args");
    char params_info[MAX_BUF];
    extract_params(args_val, params_info, sizeof(params_info));

    // 함수 본문 내 if 조건문 개수 (함수 정의인 경우)
    int if_count = 0;
    if (nodetype && strcmp(nodetype, "FuncDef") == 0)
    {
        json_value body_val = json_get(func_node, "body");
        if_count = count_if_nodes(body_val);
    }

    printf("Function: %s\n", func_name);
    printf("Return Type: %s\n", return_type);
    printf("Parameters:\n%s", params_info);
    if (nodetype && strcmp(nodetype, "FuncDef") == 0)
        printf("if-condition count: %d\n", if_count);
    printf("\n");

    // 만약 return_type가 PtrDecl 처리로 동적할당되었다면 free()
    if (return_type[0] == '*')
        free(return_type);
}

int main(void)
{
    // ast.json 파일을 fopen()을 사용하여 읽어 들임
    FILE *fp = fopen("ast.json", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "ast.json 파일을 열 수 없습니다.\n");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = (char *)malloc(filesize + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "메모리 할당 에러\n");
        fclose(fp);
        return 1;
    }

    size_t read_size = fread(buffer, 1, filesize, fp);
    buffer[read_size] = '\0';
    fclose(fp);

    // json_create() 함수를 이용하여 문자열을 JSON 객체로 변환
    json_value ast = json_create(buffer);
    free(buffer);

    if (ast.type == JSON_UNDEFINED)
    {
        fprintf(stderr, "ast.json 파일을 파싱하지 못했습니다.\n");
        return 1;
    }

    // AST 최상위 노드 배열은 "ext" 필드에 위치
    json_value ext = json_get(ast, "ext");
    if (ext.type != JSON_ARRAY)
    {
        fprintf(stderr, "ast.json의 ext 필드가 배열 형식이 아닙니다.\n");
        return 1;
    }

    int total_functions = 0;
    json_array *ext_arr = (json_array *)ext.value;
    for (int i = 0; i <= ext_arr->last_index; i++)
    {
        json_value node = ext_arr->values[i];
        json_value nodetype_val = json_get(node, "_nodetype");
        if (nodetype_val.type != JSON_STRING)
            continue;
        char *nodetype = get_json_string(nodetype_val);
        // 함수 정의: FuncDef
        if (strcmp(nodetype, "FuncDef") == 0)
        {
            total_functions++;
            process_function(node);
        }
        // 함수 선언: Decl이고 내부 type._nodetype가 FuncDecl인 경우
        else if (strcmp(nodetype, "Decl") == 0)
        {
            json_value type_val = json_get(node, "type");
            json_value funcdecl_val = json_get(type_val, "_nodetype");
            if (funcdecl_val.type == JSON_STRING &&
                strcmp(get_json_string(funcdecl_val), "FuncDecl") == 0)
            {
                total_functions++;
                process_function(node);
            }
        }
    }
    printf("Total number of functions: %d\n", total_functions);
    return 0;
}
