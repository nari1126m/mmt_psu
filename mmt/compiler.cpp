#include "json.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <windows.h>
using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

struct ValueHolder;

string resolveImportPath(const string& currentFilePath, const string& importFilePath) {
    fs::path base = fs::path(currentFilePath).parent_path(); // path ของไฟล์แม่
    fs::path full = base / importFilePath;                   // join path ใหม่
    return fs::absolute(full).string();                      // คืน path แบบเต็ม
}

struct Token {
	string type;
	string value;
	int line;
	int column;
};

class ASTNode {
public:
	Token token;
	ASTNode(const Token &t) :
		token(t) {}
	virtual string print() const = 0;
	virtual ~ASTNode() = default;
};
string ast_json(string content);

using Value = std::shared_ptr<ValueHolder>;
string valueToString(const Value& val); // ประกาศก่อน เพราะใช้แบบเรียกซ้ำได้

using ASTNodePtr = shared_ptr<ASTNode>; // to manage memory
struct functionDef {
	string name;
	vector<string> parameter;
	json body;
};

Value evalFunctionFromParts(const vector<string> &params, const json &body,
							const vector<Value> &args);

 //shared_ptr<ASTNode> parseFunctionFromJSON(const json &j);

Value evalFunctionFromNode(const json &funcNode, const vector<Value> &args);
void evalProgram(const json &programAST);

struct ValueHolder {
	using ArraY = vector<Value>;
	using ObjecT = unordered_map<string, Value>;
	variant<monostate, int, double, string, bool, ArraY, ObjecT> data;
	ValueHolder() = default;
	ValueHolder(const decltype(data) &d) :
		data(d) {}
};
struct EnvStruct {
	Value value;
	// std::string type;
	bool isConst;
};



template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

string valueToString(const Value& val) {
    return std::visit(overloaded{
        [](int v) -> std::string { return std::to_string(v); },
        [](double v) -> std::string { return std::to_string(v); },
        [](const std::string& v) -> std::string { return "\"" + v + "\""; },
        [](bool v) -> std::string { return v ? "true" : "false"; },
        [](const ValueHolder::ArraY& vec) -> std::string {
            std::string result = "[";
            bool first = true;
            for (const auto& item : vec) {
                if (!first) result += ", ";
                result += valueToString(item);
                first = false;
            }
            result += "]";
            return result;
        },
        [](const ValueHolder::ObjecT& obj) -> std::string {
            std::string result = "{";
            bool first = true;
            for (const auto& [key, val] : obj) {
                if (!first) result += ", ";
                result += "\"" + key + "\": " + valueToString(val);
                first = false;
            }
            result += "}";
            return result;
        },
        [](std::monostate) -> std::string {
            return "null";
        }
    }, val->data);
}




void syntaxError(const Token &token, const string &msg) {
	stringstream ss;
	ss << "ไวยากรณ์ผิดพลาดที่บรรทัด " << token.line << " คอลัมน์ " << token.column
	   << ": " << msg << " (พบ '" << token.value << "')";
	cerr << ss.str() << "";
	std::exit(1);
}
void lexerError(size_t line, size_t col, const string &msg,
				const string &context) {
	stringstream ss;
	ss << "Lexer error at line " << line << ", column " << col << ": " << msg
	   << "\nContext: '" << context << "'";
	cerr << ss.str() << "";
	std::exit(1);
}

unordered_map<string, unordered_map<string, functionDef>> importModules;
unordered_map<string, functionDef> exportedFunctions;
std::vector<std::unordered_map<std::string, EnvStruct>> env;
unordered_map<string, functionDef> functionTable;

bool getBool(Value val) {
	if (!val) {
		cerr << "ค่าที่ส่งมาตรวจสอบเป็น nullptr\n";
		exit(1);
	}

	if (holds_alternative<bool>(val->data)) {
		return get<bool>(val->data);
	}

	cerr << "ค่าที่ส่งมาตรวจสอบไม่ใช่ boolean\n";
	exit(1);
}

EnvStruct *lookvar(const string &name, int line, int column) {
	for (int i = env.size() - 1; i >= 0; i--) {
	
		if (env[i].count(name)) {

			return &env[i][name];
		}

	}
	cerr << "ไม่พบตัวแปร " << name << " ในขอบเขตนี้ ที่บรรทัด " << line << "คอลัม์"
		 << column << "";
	exit(1);
}
Value getVar(const string &name, int line, int column) {
    int scopeIndex = (int)env.size() - 1;  // เริ่มที่ scope สุดท้าย
    for (auto enV = env.rbegin(); enV != env.rend(); ++enV, --scopeIndex) {
       
        if (enV->count(name)) {
            return (*enV)[name].value;
        }
    }
    cerr << "ไม่พบตัวแปร " << name << " ในขอบเขตนี้ ที่บรรทัด " << line << " คอลัมน์ " << column << "";
    exit(1);
}




void declare(const string &name, Value val, const bool &isConst, int line, int column) {
    if (env.back().count(name)) {
        cerr << "มีการประกาศตัวแปร " << name << " แล้วในขอบเขตนี้ ที่บรรทัด " << line
             << " คอลัมน์ " << column << "";
        exit(1);
    }
    env.back()[name] = {val, isConst};
}


void setvar(const string &name, Value val, int line, int column) {
    EnvStruct *entry = lookvar(name, 0, 0);
    if (entry->isConst == true) {
        cerr << "ไม่สามารถกำหนดค่าคงที่" << name << "ได้ ที่บรรทัด " << line
             << " คอลัมน์ " << column << "";
        std::exit(1);
    }
    // หาตำแหน่งตัวแปรใน scope ที่อยู่ลึกสุดที่เจอชื่อ name
    for (int i = env.size() - 1; i >= 0; i--) {
        if (env[i].count(name)) {
            env[i][name].value = val;  // อัปเดตค่าตัวแปร
            return;
        }
    }
    // ถ้าไม่เจอเลย
    cerr << "ไม่พบตัวแปร " << name << " ในขอบเขตนี้ ที่บรรทัด " << line
         << " คอลัมน์ " << column << "";
    exit(1);
}


// evalExper
// shared_ptr<ASTNode> parseFunctionFromJSON(const json &j);
Value evalExpr(const json &expr) {

	if (!expr.is_object()) {
		cerr << "❌ expr ไม่ใช่ json object แต่เป็น: " << expr << "";
		exit(1);
	}

	string type = expr["type"];
	if (type == "int") {
		return make_shared<ValueHolder>(expr["value"].get<int>());
	} else if (type == "float") {
		return make_shared<ValueHolder>(expr["value"].get<double>());
	} else if (type == "bool") {
		return make_shared<ValueHolder>(expr["value"].get<bool>());
	} else if (type == "string") {
		return make_shared<ValueHolder>(expr["value"].get<string>());
	} else if (type == "null") {
		return make_shared<ValueHolder>(monostate{});
	} else if (type == "variable") {
		EnvStruct *var =  lookvar(expr["name"], expr["line"], expr["column"]);
		return var->value;
	}

	else if (type == "ArrayLiterel") {
		ValueHolder::ArraY arr;
		for (const auto &a : expr["element"]) {
			arr.push_back(evalExpr(a));
		}
		return make_shared<ValueHolder>(arr);
	} else if (type == "ObjectLiteral") {
		ValueHolder::ObjecT obj;

		for (const auto &prop : expr["properties"]) {
			Value keyVal = evalExpr(prop["key"]);

			if (!holds_alternative<string>(keyVal->data)) {
				cerr << "ผิดพลาด: คีย์ในออบเจ็กต์ต้องเป็นข้อความ ที่บรรทัด: "
					 << prop["key"]["line"]
					 << " คอลัมน์: " << prop["key"]["column"] << "";
				std::exit(1);
			}

			string key = get<string>(keyVal->data);
			Value val = evalExpr(prop["value"]);
			obj[key] = val;
		}

		return make_shared<ValueHolder>(obj);
	}

	// pimary
	else if (type == "unaryOp") {
		Value operand = evalExpr(expr["operand"]);
		string op = expr["operator"];
		if (op == "NOT") {
			if (holds_alternative<bool>(operand->data)) {
				return make_shared<ValueHolder>(!get<bool>(operand->data));
			} else if (holds_alternative<int>(operand->data)) {
				return make_shared<ValueHolder>(!get<int>(operand->data));
			} else if (holds_alternative<double>(operand->data)) {
				return make_shared<ValueHolder>(!get<double>(operand->data));
			}
			cerr << "ไม่สามารถหา นิเสธของ " << valueToString(operand) << "";
			exit(1);
		} else if (op == "BITWISE_NOT") {
			if (holds_alternative<bool>(operand->data)) {
				return make_shared<ValueHolder>(~get<bool>(operand->data));
			} else if (holds_alternative<int>(operand->data)) {
				return make_shared<ValueHolder>(~get<int>(operand->data));
			}
			cerr << "ไม่สามารถ สลับบิต ของ " << valueToString(operand) << "";
			exit(1);
		} else if (op == "INCREMENT") {
			if (holds_alternative<int>(operand->data)) {
				return make_shared<ValueHolder>(get<int>(operand->data)++);
			}
			cerr << "ไม่สามารถ เพิ่มค่า ของ " << valueToString(operand) << "";
			exit(1);
		} else if (op == "DECREMENT") {
			if (holds_alternative<int>(operand->data)) {
				return make_shared<ValueHolder>(get<int>(operand->data)--);
			}
			cerr << "ไม่สามารถ ลดค่า ของ " << valueToString(operand) << "";
			exit(1);
		}
		 else if (op == "SUBTRACTION") {
					if (holds_alternative<int>(operand->data)) {
						return make_shared<ValueHolder>(-(get<int>(operand->data)));
					}else if (holds_alternative<double>(operand->data)) {
						return make_shared<ValueHolder>(-(get<double>(operand->data))--);
					}
					cerr << "ค่านี้ " << valueToString(operand) <<"ไม่สามารถติดลบได้"<< "";
					exit(1);
				}
	} else if (type == "ln") {
		Value val = evalExpr(expr["value"]);
		if (holds_alternative<int>(val->data)) {
			return make_shared<ValueHolder>(log(get<int>(val->data)));
		} else if (holds_alternative<double>(val->data)) {
			return make_shared<ValueHolder>(log(get<double>(val->data)));
		}
		cerr << "ไม่สามารถหาค่า ลอการิทึมธรรมชาติ ของ" << val
			 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
			 << "";
	} else if (type == "binaryOp") {
		string op = expr["Op"];
		Value left = evalExpr(expr["left"]);
		Value right = evalExpr(expr["right"]);
		if (op == "EXPONENTIATION") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {

				return make_shared<ValueHolder>(
					pow(get<int>(left->data), get<int>(right->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					pow(get<double>(left->data), get<double>(right->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					pow(get<double>(left->data), get<int>(right->data)));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					pow(get<int>(left->data), get<double>(right->data)));
			}

			cerr << "ไม่สามารถยกกำลัง " << valueToString(left) << " กับ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "ROOT") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					pow(get<int>(right->data), 1.0 / get<int>(left->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					pow(get<double>(right->data), 1.0 / get<double>(left->data)));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					pow(get<double>(right->data), 1.0 / get<int>(left->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					pow(get<double>(right->data), 1.0 / get<int>(left->data)));
			}

			cerr << "ไม่สามารถถอดรากที่ " << valueToString(left) << " ของ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "MULTIPLICATION") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) *
												get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(get<double>(left->data) *
												get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) *
												get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<double>(left->data) *
												get<int>(right->data));
			}
			cerr << "ไม่สามารถคูณ " << valueToString(left) << "กับ" << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);

		} else if (op == "DIVISION") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(static_cast<double>(get<int>(left->data)) /
												static_cast<double>(get<int>(right->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(get<double>(left->data) /
												get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) /
												get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<double>(left->data) /
												get<int>(right->data));
			}
			cerr << "ไม่สามารถหาร " << valueToString(left) << "กับ" << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);

		} else if (op == "FLOORDIVISION") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					floor(get<int>(left->data) / get<int>(right->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					floor(get<double>(left->data) / get<double>(right->data)));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					floor(get<int>(left->data) / get<double>(right->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					floor(get<double>(left->data) / get<int>(right->data)));
			}
			cerr << "ไม่สามารถหารเอาส่วน " << valueToString(left) << "กับ" << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);

		} else if (op == "MODULAS") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) %
												get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					fmod(get<double>(left->data), get<double>(right->data)));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					fmod(static_cast<double>(get<int>(left->data)),
						 get<double>(right->data)));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					fmod(get<double>(left->data),
						 static_cast<double>(get<int>(right->data))));
			}
			cerr << "ไม่สามารถMOD " << valueToString(left) << "กับ" << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);

		}

		else if (op == "ADDITION") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) +
												get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(get<double>(left->data) +
												get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) +
												get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<double>(left->data) +
												get<int>(right->data));
			} else if (holds_alternative<string>(left->data) &&
					   holds_alternative<string>(right->data)) {
				return make_shared<ValueHolder>(get<string>(left->data) +
												get<string>(right->data));
			} else if (holds_alternative<ValueHolder::ObjecT>(left->data) &&
					   holds_alternative<ValueHolder::ObjecT>(right->data)) {
				auto merged = get<ValueHolder::ObjecT>(left->data);
				const auto &rightobj = get<ValueHolder::ObjecT>(right->data);
				for (const auto &[k, v] : rightobj) {
					merged[k] = v;
				}
				return make_shared<ValueHolder>(merged);
			} else if (holds_alternative<ValueHolder::ArraY>(left->data) &&
					   holds_alternative<ValueHolder::ArraY>(right->data)) {
				auto merged = get<ValueHolder::ArraY>(left->data);
				const auto &rightarr = get<ValueHolder::ArraY>(right->data);
				merged.insert(merged.end(), rightarr.begin(), rightarr.end());
				return make_shared<ValueHolder>(merged);
			}

			cerr << "ไม่สามารถบวก " << valueToString(left) << " กับ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "SUBTRACTION") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) - get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) - get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) - get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) - get<int>(right->data));
			}
			cerr << "ไม่สามารถลบ " << valueToString(left) << " กับ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "SHIFT_LEFT") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data)
												<< get<int>(right->data));
			}
			cerr << "ไม่สามารถ เลื่อนบิตของ " << valueToString(left) << " ไปทางซ้าย " << valueToString(right)
				 << "ตำแหน่ง ที่บรรทัด: " << expr["line"]
				 << " คอลัมน์: " << expr["column"] << "";
			exit(1);
		} else if (op == "SHIFT_RIGHT") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) >>
												get<int>(right->data));
			}
			cerr << "ไม่สามารถ เลื่อนบิตของ " << valueToString(left) << " ไปทางซ้าย " << valueToString(right)
				 << "ตำแหน่ง ที่บรรทัด: " << expr["line"]
				 << " คอลัมน์: " << expr["column"] << "";
			exit(1);
		} else if (op == "GREATER") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) > get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) > get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) > get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) > get<int>(right->data));
			}
			cerr << "ไม่สามารถเปรียบเทียบมากกว่า " << valueToString(left) << " กับ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "LESSER") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) < get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) < get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) < get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) < get<int>(right->data));
			}
			cerr << "ไม่สามารถเปรียบเทียบน้อยกว่า " << valueToString(left) << " กับ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "GREATEROREQUAL") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) >= get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) >= get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) >= get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) >= get<int>(right->data));
			}
			cerr << "ไม่สามารถเปรียบเทียบมากกว่าหรือเท่ากับ " << valueToString(left) << " กับ "
				 << valueToString(right) << " ที่บรรทัด: " << expr["line"]
				 << " คอลัมน์: " << expr["column"] << "";
			exit(1);
		} else if (op == "LESSEROREQUAL") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) <= get<int>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) <= get<double>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<double>(right->data)) {
				return make_shared<ValueHolder>(
					get<int>(left->data) <= get<double>(right->data));
			} else if (holds_alternative<double>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(
					get<double>(left->data) <= get<int>(right->data));
			}
			cerr << "ไม่สามารถเปรียบเทียบน้อยกว่าหรือเท่ากับ " << valueToString(left) << " กับ "
				 << valueToString(right) << " ที่บรรทัด: " << expr["line"]
				 << " คอลัมน์: " << expr["column"] << "";
			exit(1);

		} else if (op == "EQUALTO") {
			return make_shared<ValueHolder>(left->data == right->data);
		} else if (op == "NOTEQUAL") {
			return make_shared<ValueHolder>(left->data != right->data);
		} else if (op == "BITWISE_AND") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) &
												get<int>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) &
												get<bool>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) &
												get<bool>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) &
												get<int>(right->data));
			}
			cerr << "ไม่สามารถใช้ ตัวนำเนินการ & กับ " << valueToString(left) << " และ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);

		} else if (op == "XOR") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) ^
												get<int>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) ^
												get<bool>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) ^
												get<bool>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) ^
												get<int>(right->data));
			}
			cerr << "ไม่สามารถใช้ ตัวนำเนินการ ซอร์ กับ " << valueToString(left) << " และ " << valueToString(right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "BITWISE_OR") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) |
												get<int>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) |
												get<bool>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) |
												get<bool>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) |
												get<int>(right->data));
			}
			cerr << "ไม่สามารถใช้ ตัวนำเนินการ | กับ " << valueToString(left) << " และ " << (right)
				 << " ที่บรรทัด: " << expr["line"] << " คอลัมน์: " << expr["column"]
				 << "";
			exit(1);
		} else if (op == "AND") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) &&
												get<int>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) &&
												get<bool>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) &&
												get<bool>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) &&
												get<int>(right->data));
			}
			cerr << "ไม่สามารถใช้ ตัวนำเนินการ 'และ' กับ " << valueToString(left) << " และ "
				 << valueToString(right) << " ที่บรรทัด: " << expr["line"]
				 << " คอลัมน์: " << expr["column"] << "";
			exit(1);
		} else if (op == "OR") {
			if (holds_alternative<int>(left->data) &&
				holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) ||
												get<int>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) ||
												get<bool>(right->data));
			} else if (holds_alternative<int>(left->data) &&
					   holds_alternative<bool>(right->data)) {
				return make_shared<ValueHolder>(get<int>(left->data) ||
												get<bool>(right->data));
			} else if (holds_alternative<bool>(left->data) &&
					   holds_alternative<int>(right->data)) {
				return make_shared<ValueHolder>(get<bool>(left->data) ||
												get<int>(right->data));
			}
			cerr << "ไม่สามารถใช้ ตัวนำเนินการ 'หรือ' กับ " << valueToString(left) << " และ "
				 << valueToString(right) << " ที่บรรทัด: " << expr["line"]
				 << " คอลัมน์: " << expr["column"] << "";
			exit(1);
		}

	} // the end of binatyOp
	else if (type == "Convert") {

		string typE = expr["target"];
		Value exp = evalExpr(expr["expression"]);
		if (typE == "INTEGER") {
			if (holds_alternative<string>(exp->data)) {
				return make_shared<ValueHolder>(stoi(get<string>(exp->data)));
			} else if (holds_alternative<bool>(exp->data)) {
				return make_shared<ValueHolder>((int)get<bool>(exp->data));
			} else if (holds_alternative<double>(exp->data)) {
				return make_shared<ValueHolder>(
					static_cast<int>(get<double>(exp->data)));
			} else if (holds_alternative<int>(exp->data)) {
				return make_shared<ValueHolder>(get<int>(exp->data));
			}
		} else if (typE == "FLOAT") {
			if (holds_alternative<string>(exp->data)) {
				return make_shared<ValueHolder>(stod(get<string>(exp->data)));
			} else if (holds_alternative<int>(exp->data)) {
				return make_shared<ValueHolder>(
					static_cast<double>(get<int>(exp->data)));
			} else if (holds_alternative<double>(exp->data)) {
				return make_shared<ValueHolder>(get<double>(exp->data));
			}
		} else if (typE == "STRING") {
			if (holds_alternative<int>(exp->data)) {
				return make_shared<ValueHolder>(to_string(get<int>(exp->data)));
			} else if (holds_alternative<double>(exp->data)) {
				return make_shared<ValueHolder>(
					to_string(get<double>(exp->data)));
			} else if (holds_alternative<string>(exp->data)) {
				return make_shared<ValueHolder>(get<string>(exp->data));
			}
		} else if (typE == "BOOLEAN") {
			if (holds_alternative<int>(exp->data)) {
				return make_shared<ValueHolder>(
					static_cast<bool>(get<int>(exp->data)));
			} else if (holds_alternative<bool>(exp->data)) {
				return make_shared<ValueHolder>(get<bool>(exp->data));
			}
		}

		cerr << "ไม่สามารถแปลงเป็นชนิด: " << typE << " ที่บรรทัด: " << expr["line"]
			 << " คอลัมน์: " << expr["column"] << "";
		exit(1);
	}else if (type == "ObjectAccess") {
	    Value obj = evalExpr(expr["object"]);
	    json keyNode = expr["key"];
	    string key;

	    // ตรวจสอบว่า key เป็น variable node หรือไม่ (dot notation)
	    if (keyNode["type"] == "variable") {
	        key = keyNode["name"].get<string>();
	    } else {
	        Value keyVal = evalExpr(keyNode);
	        if (!std::holds_alternative<std::string>(keyVal->data)) {
	            std::cerr << "ผิดพลาด: ออบเจ็ต คีย์ต้องเป็น ข้อความ ที่บรรทัด "
	                      << expr["line"] << ", คอลัม์ " << expr["column"] << "";
	            exit(1);
	        }
	        key = std::get<std::string>(keyVal->data);
	    }

	    // ส่วนที่เหลือเหมือนเดิม
	    if (!std::holds_alternative<ValueHolder::ObjecT>(obj->data)) {
	        std::cerr << "ผิดพลาด: ไม่สามารถเข้าถึง คีย์ '" << key
	                  << "' บน ออบเจกต์ที่ยังไม่ประกาศ ที่บรรทัด " << expr["line"]
	                  << ", คอลัม์ " << expr["column"] << "";
	        exit(1);
	    }

	    auto &objMap = std::get<ValueHolder::ObjecT>(obj->data);
	    if (!objMap.count(key)) {
	        std::cerr << "พิดพลาด: คีย์นี้ '" << key << "' ไม่พบใน ออบเจกต์ ที่บรรทัด "
	                  << expr["line"] << ", คอลัม์ " << expr["column"] << "";
	        exit(1);
	    }

	    return objMap[key];
	} else if (type == "ArrayAccess") {
		Value arrayVal = evalExpr(expr["array"]);
		Value indexVal = evalExpr(expr["index"]);
		int index;

		if(holds_alternative<int>(indexVal->data) && get<int>(indexVal->data) >= 0){
			index = get<int>(indexVal->data);
		}else if(holds_alternative<double>(indexVal->data)){
			double d = get<double>(indexVal->data);
			if(d == static_cast<int>(d) && d>=0){
				index = static_cast<int>(get<double>(indexVal->data));
			}else{
				cerr<<"ผิดพลาด: ไม่สามารถเข้นถึงชุดข้อมูลด้วย ดัชนีที่เป็นทศนิยม ที่ บรรทัด "
					  << expr["line"] << ", คอลัมน์ " << expr["column"] <<"";
				exit(1);
			}
		}else{
			cerr<<"ผิดพลาด: ไม่สามารถเข้นถึงชุดข้อมูลด้วย ดัชนีที่ไม่ใช่ตัวเลข ที่ บรรทัด "
				  << expr["line"] << ", คอลัมน์ " << expr["column"] <<"";
		}



		if (!(std::holds_alternative<ValueHolder::ArraY>(arrayVal->data) ||
			  std::holds_alternative<std::string>(arrayVal->data))) {
			std::cerr << "ผิดพลาด: ไม่สามารถเข้าถึงข้อมูลประเภทนี้ด้วยดัชนี ที่ บรรทัด "
					  << expr["line"] << ", คอลัมน์ " << expr["column"] << "";
			exit(1);
		}

		if (std::holds_alternative<ValueHolder::ArraY>(arrayVal->data)) {
			auto &arr = std::get<ValueHolder::ArraY>(arrayVal->data);
			if (index >= static_cast<int>(arr.size())) {
				std::cerr << "ผิดพลาด : ดัชนีเกินขอบเขต ที่ บรรทัด "
						  << expr["line"] << ", คอลัมน์ " << expr["column"] << "";
				exit(1);
			}
			return arr[index];
		} else {
			auto &arr = std::get<std::string>(arrayVal->data);
			if (index >= static_cast<int>(arr.size())) {
				std::cerr << "ผิดพลาด : ดัชนีเกินขอบเขต ที่ บรรทัด "
						  << expr["line"] << ", คอลัมน์ " << expr["column"] << "";
				exit(1);
			}
			return make_shared<ValueHolder>(std::string(1, arr[index]));
		}


	}else if (type == "FunctionCall") {
	    string funcname;
	    try {
	        funcname = expr["name"]["name"].get<string>();
	    } catch (json::type_error& e) {
	        cerr << "ชื่อโปรแกรมไม่ถูกต้อง (ต้องเป็นตัวแปร) ที่บรรทัด "
	             << expr["line"] << " คอลัมน์ " << expr["column"] << "";
	        exit(1);
	    }

	    string ns;
	    if (expr.contains("namespace") && !expr["namespace"].is_null()) {
	        try {
	            ns = expr["namespace"]["name"].get<string>();
	        } catch (json::type_error& e) {
	            cerr << "Namespace ต้องเป็นตัวแปร ที่บรรทัด "
	                 << expr["line"] << " คอลัมน์ " << expr["column"] << "";
	            exit(1);
	        }
	    }

	    vector<Value> args;
	    for (auto &arg : expr["argument"]) {
	        args.push_back(evalExpr(arg));
	    }

	    // เรียกจาก namespace
	    if (!ns.empty()) {
	        if (importModules.find(ns) == importModules.end()) {
	            cerr << "ไม่พบเนมสเปซ: \"" << ns << "\" ที่บรรทัด "
	                 << expr["line"] << " คอลัมน์ " << expr["column"] << "";
	            exit(1);
	        }
	        auto &functions = importModules[ns];
	        if (functions.find(funcname) == functions.end()) {
	            cerr << "โปรแกรม \"" << funcname << "\" ไม่พบในเนมสเปซ \"" << ns
	                 << "\" ที่บรรทัด " << expr["line"] << " คอลัมน์ " << expr["column"] << "";
	            exit(1);
	        }
	        const functionDef &def = functions[funcname];
	        return evalFunctionFromParts(def.parameter, def.body, args);
	    }
	    // เรียกฟังก์ชันโลคัล
	    else {
	        if (functionTable.find(funcname) == functionTable.end()) {
	            cerr << "โปรแกรม '" << funcname << "' ยังไม่ถูกประกาศ ที่บรรทัด "
	                 << expr["line"] << " คอลัมน์ " << expr["column"] << "";
	            exit(1);
	        }
	        const functionDef& def = functionTable[funcname];
	        if (args.size() != def.parameter.size()) {
	            cerr << "จำนวนอากิวเมนต์ไม่ตรงกัน สำหรับ '" << funcname << "' ต้องการ "
	                 << def.parameter.size() << ", ได้รับ " << args.size()
	                 << " ที่บรรทัด " << expr["line"] << " คอลัมน์ " << expr["column"] << "";
	            exit(1);
	        }
	        return evalFunctionFromParts(def.parameter, def.body, args);
	    }
	}
 else if (type == "Length") {
		Value target = evalExpr(expr["target"]);
		if (holds_alternative<ValueHolder::ArraY>(target->data)) {
			return std::make_shared<ValueHolder>(static_cast<int>(
				std::get<ValueHolder::ArraY>(target->data).size()));
		} else if (holds_alternative<ValueHolder::ObjecT>(target->data)) {
			return std::make_shared<ValueHolder>(static_cast<int>(
				std::get<ValueHolder::ObjecT>(target->data).size()));
		} else if (holds_alternative<string>(target->data)) {
			return std::make_shared<ValueHolder>(
				static_cast<int>(std::get<string>(target->data).size()));
		}
		std::cerr << "เกิดข้อพิดพลาด: ขนาด() ไม่รองรับข้อมูลประเภทนี้ ที่บรรทัด : "
				  << expr["line"] << ", คอลัม์: " << expr["column"] << "";
		exit(1);
	}
	cerr << "ไม่มี expression นี้ ที่ บรรทัด " << expr["line"]
		 << ", คอลัม์: " << expr["column"] << "";
	exit(1);
}
void printValue(const Value &val) {
	struct {
		void operator()(std::monostate) const { std::cout << "ว่าง"; }
		void operator()(int v) const { std::cout << v; }
		void operator()(double v) const { std::cout << v; }
		void operator()(const std::string &v) const {
			std::cout <<v;
		}
		void operator()(bool v) const { std::cout << (v ? "จริง" : "เท็จ"); }
		void operator()(const ValueHolder::ArraY &arr) const {
			std::cout << "[";
			bool first = true;
			for (const auto &e : arr) {
				if (!first)
					std::cout << ", ";
				printValue(e); // เรียกซ้ำ
				first = false;
			}
			std::cout << "]";
		}
		void operator()(const ValueHolder::ObjecT &obj) const {
			std::cout << "{";
			bool first = true;
			for (const auto &[k, v] : obj) {
				if (!first)
					std::cout << ", ";
				std::cout << '"' << k << "\": ";
				printValue(v); // เรียกซ้ำ
				first = false;
			}
			std::cout << "}";
		}
	} visitor;

	std::visit(visitor, val->data);
}
struct ReturnException : public std::exception {
	Value returnValue;
	ReturnException(const Value &val) :
		returnValue(val) {}
};

class BreakException : public std::exception {};
class ContinueException : public std::exception {};

// evalStatement

Value evalStatement(const json &stmt) {
	if (!stmt.is_object()) {
		cerr << "❌ stmt ไม่ใช่ json object แต่เป็น: " << stmt << "";
		exit(1);
	}


	string type = stmt["type"];

	if (type == "print") {
	  // ตรวจสอบว่า expression เป็น array
	  if (!stmt.contains("expression") || !stmt["expression"].is_array()) {
	    cerr << " print ต้องการ array ของ expressions\n";
	    exit(1);
	  }

	  // ดึง array ออกมาก่อน
	  json exprArray = stmt["expression"];

	  for (const auto& expr : exprArray) {
	    Value val = evalExpr(expr);
	    printValue(val);
	   // cout << " ";
	  }
	  cout << "";
	  return nullptr;
	} else if (type == "variableDeclaration") {
		string name = stmt["variable"]["name"];
		bool isConst = (stmt["vicedatatype"] == "const");
		Value val;
		if (!stmt["value"].is_null()) {
			val = evalExpr(stmt["value"]);
		} else {
			val = make_shared<ValueHolder>(monostate{});
		}

		declare(name, val, isConst, stmt["line"], stmt["column"]);
		return nullptr;
	} else if (type == "block") {
		env.push_back({});
		for (const auto &s : stmt["statements"]) {
			evalStatement(s);
		}
		env.pop_back();
		return nullptr;
	} else if (type == "if") {
		if (get<bool>(evalExpr(stmt["condition"])->data)) {
			for (const auto &s : stmt["body"]["statements"]) {
				Value result = evalStatement(s);
				if (result)
					return result;
			}
		} else {
			// ✅ ตรวจว่า elif มีจริง และเป็น array ที่ไม่ว่าง
			if (stmt.contains("elif") && stmt["elif"].is_array() && !stmt["elif"].empty()) {
				for (const auto &elifStmt : stmt["elif"]) {
					if (get<bool>(evalExpr(elifStmt["condition"])->data)) {
						for (const auto &s : elifStmt["body"]["statements"]) {
							Value result = evalStatement(s);
							if (result)
								return result;
						}
						return nullptr; // ✅ ถ้า elif ตรงเงื่อนไข ให้หยุดที่นี่
					}
				}
			}

			// ✅ else ทำงานเมื่อไม่มี elif ใดๆ ตรงเลย
			if (stmt.contains("else") && stmt["else"].is_object()) {
				for (const auto &s : stmt["else"]["body"]["statements"]) {
					Value result = evalStatement(s);
					if (result)
						return result;
				}
			}
		}
		return nullptr;
	}

 else if (type == "assignment") {
		const auto &target = stmt["variable"];
		Value val = evalExpr(stmt["value"]);
		if (target["type"] == "variable") {
			string name = target["name"];
			setvar(name, val, stmt["line"], stmt["column"]);
		} else if (target["type"] == "ObjectAccess") {
			Value obj = evalExpr(target["object"]);
			Value key = evalExpr(target["key"]);
			if (!holds_alternative<ValueHolder::ObjecT>(obj->data)) {
				cerr << "ค่าที่จะกำหนดไม่ใช่ ออบเจต์ ที่บรรทัด " << stmt["line"]
					 << " คอลัมน์ " << stmt["column"] << "";
				exit(1);
			}
			get<ValueHolder::ObjecT>(obj->data)[get<string>(key->data)] = val;

		} else if (target["type"] == "ArrayAccess") {
			Value arr = evalExpr(target["array"]);
			int index = get<int>(evalExpr(target["index"])->data);
			if (!holds_alternative<ValueHolder::ArraY>(arr->data)) {
				cerr << "ค่าที่จะกำหนดไม่ใช่ ชุดข้อมูล ที่บรรทัด " << stmt["line"]
					 << " คอลัมน์ " << stmt["column"] << "";
				exit(1);
			}
			auto &vec = get<ValueHolder::ArraY>(arr->data);
			if (index < 0 || index >= vec.size()) {
				cerr << "index ของ array เกินขอบเขต ที่บรรทัด " << stmt["line"]
					 << " คอลัมน์ " << stmt["column"] << "";
				exit(1);
			}
			vec[index] = val;
		} else {
			cerr << "ไม่สามารถกำหนดค่าสิ่งนี้ได้ ที่บรรทัด " << stmt["line"] << " คอลัมน์ "
				 << stmt["column"] << "";
			exit(1);
		}

		return nullptr;
	} else if (type == "input") {
		string name = stmt["variable"]["name"];
		string in;
		getline(cin, in);
		setvar(name, make_shared<ValueHolder>(in), stmt["line"],
			   stmt["column"]);
		return nullptr;
	} else if (type == "Break") {
		throw BreakException();
	} else if (type == "Continue") {
		throw ContinueException();
	} else if (type == "whileloop") {
		while (getBool(evalExpr(stmt["condition"]))) {
			try {

				 env.push_back({});


				// ทำซ้ำ block
				const json &body = stmt["body"];
				if (body["type"] == "block") {
					for (const auto &s : body["statements"]) {
						evalStatement(s);
					}
				} else {
					evalStatement(body);
				}

				env.pop_back();


			} catch (const ContinueException &) {
				// ข้ามไปยังรอบถัดไป
				continue;
			} catch (const BreakException &) {
				// ออกจากลูป
				break;
			}
		}
		return nullptr;
	} else if (type == "dowhileloop") {
		do {
			try {

				env.push_back({});


				// ทำซ้ำ block
				const json &body = stmt["body"];
				if (body["type"] == "block") {
					for (const auto &s : body["statements"]) {
						evalStatement(s);
					}
				} else {
					evalStatement(body);
				}

				env.pop_back();


			} catch (const ContinueException &) {
				// ข้ามไปยังรอบถัดไป
				continue;
			} catch (const BreakException &) {
				// ออกจากลูป
				break;
			}
		} while (getBool(evalExpr(stmt["condition"])));
		return nullptr;
	}else if (type == "forloop") {
    // scope สำหรับ initialization และตัวแปรของลูป
    env.push_back({});

    evalStatement(stmt["initialization"]);


    while (getBool(evalExpr(stmt["condition"]))) {
        try {

            // ✅ สร้าง scope ใหม่ให้ body ทุกครั้ง
            env.push_back({});

            const json &body = stmt["body"];
            if (body["type"] == "block") {
                for (const auto &s : body["statements"]) {
                    evalStatement(s);
                }
            } else {
                evalStatement(body);
            }

            // ✅ ลบ scope ของ body ออกเมื่อจบรอบ
            env.pop_back();

        } catch (const ContinueException &) {
            env.pop_back(); // ลบ scope body ก่อน continue
            continue;
        } catch (const BreakException &) {
            env.pop_back(); // ลบ scope body ก่อน break
            break;
        }

        evalStatement(stmt["changevalue"]);
    }

    env.pop_back();

    return nullptr;
}
 else if (type == "return") {
		Value val = evalExpr(stmt["value"]);
		throw ReturnException(val);
		return nullptr;
	}else if (type == "functionDeclaretion") {
		string funcName = stmt["name"];
		vector<string> parameterName;
		for (const auto &a : stmt["parameter"]) {
			parameterName.push_back(a["variable"]["name"]);
		}

		functionDef func;
		func.name = funcName;
		func.parameter = parameterName; // ✅ ต้องเก็บไว้ตรงนี้
		func.body = stmt["body"]["statements"];
		functionTable[funcName] = func;
		return nullptr;
	}
else if (type == "Push") {
		Value arrayVal = evalExpr(stmt["array"]);
		Value value = evalExpr(stmt["value"]);
		if(holds_alternative<ValueHolder::ArraY>(arrayVal->data)){
			get<ValueHolder::ArraY>(arrayVal->data).push_back(value);
		}
		else if(holds_alternative<string>(arrayVal->data)){
			Value val = evalExpr(stmt["value"]);
			get<string>(arrayVal->data) += get<string>(val->data);
		}else{
			cerr << "ไม่สามารถเพิ่มสมาชิกเข้า  ชุดข้อมูลได้"
				 << " ได้ เนื่องจากไม่ใช่ชุดข้อมูล หรือ ข้อความ "
				 << "ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
			exit(1);
		}
		return nullptr;
	}
	else if (stmt["type"] == "Pop") {
		Value arrayVal = evalExpr(stmt["array"]);
		if(holds_alternative<ValueHolder::ArraY>(arrayVal->data)){
		auto &arr = get<ValueHolder::ArraY>(arrayVal->data);
		if (arr.empty()) {
			cerr << "ไม่สามารถ ดึงข้อมูลออก จาก ชุดข้อมูล ที่ว่าง "
				 << " ได้ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
			exit(1);
		}

		arr.pop_back();
		}
		else if(holds_alternative<string>(arrayVal->data)){
			auto &arr = get<string>(arrayVal->data);
			if (arr.empty()) {
				cerr << "ไม่สามารถ ดึงข้อมูลออก จาก ข้อความ ที่ว่าง "
					 << " ได้ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
				exit(1);
			}

			arr.pop_back();
			}
		else{
			cerr << "ไม่สามารถลบสมาชิกนี้ได้ '"
				 << " ได้ เนื่องจากไม่ใช่ชุดข้อมูล หรือ ข้อความ "
				 << "ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
			exit(1);
		}
		return nullptr;
	}
	else if (stmt["type"] == "Insert") {
		Value arrayVal = evalExpr(stmt["array"]);
		Value indexVal = evalExpr(stmt["index"]);
		Value valueToInsert = evalExpr(stmt["value"]);

		if (!holds_alternative<int>(indexVal->data)) {
			cerr << "ดัชนี ต้องเป็นจำนวนเต็ม "
				 << "ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
			exit(1);
		}

		int index = get<int>(indexVal->data);

		if(holds_alternative<ValueHolder::ArraY>(arrayVal->data)){
			auto &array = get<ValueHolder::ArraY>(arrayVal->data);
			if (index < 0 || index > static_cast<int>(array.size())) {
				cerr << "ดัชนี อยู่นอกขอบเขตของ ชุดข้อมูล ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
				exit(1);
			}

			array.insert(array.begin() + index, valueToInsert);
		}else if (holds_alternative<string>(arrayVal->data)) {
			auto &array = get<string>(arrayVal->data);

			if (index < 0 || index > static_cast<int>(array.size())) {
				cerr << "ดัชนีอยู่นอกขอบเขตของข้อความ ที่บรรทัด "
				     << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
				exit(1);
			}

			Value valueToInsert = evalExpr(stmt["value"]);

			// ดึง string มา insert
			const string &strToInsert = get<string>(valueToInsert->data);
			array.insert(index, strToInsert);
		}else{
			cerr << "ไม่สามารถแทรกได้ เนื่องจากค่าไม่ใช่ชุดข้อมูล หรือ ข้อความ "
				 << "ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
			exit(1);
		}


		return nullptr;
	}

 else if (stmt["type"] == "Erase") {
		Value arrayVal = evalExpr(stmt["array"]);
		Value indexVal = evalExpr(stmt["index"]);
		int index;

		if (!holds_alternative<int>(indexVal->data)) {
			if (holds_alternative<double>(indexVal->data)) {
				double b = get<double>(indexVal->data);
				if (b != static_cast<int>(b)) {
					cerr << "ดัชนีที่ต้องการลบจาก ชุดข้อมูล ต้องเป็นจำนวนเต็ม"
						 << "ที่บรรทัด: " << stmt["line"] << " คอลัมน์: " << stmt["column"]
						 << "";
					exit(1);
				}
				index = static_cast<int>(b);
			} else {
				cerr << "ดัชนีที่ต้องการลบจาก ชุดข้อมูล ต้องเป็นจำนวนเต็ม"
					 << "ที่บรรทัด: " << stmt["line"] << " คอลัมน์: " << stmt["column"]
					 << "";
				exit(1);
			}
		} else {
			index = get<int>(indexVal->data);
		}


		if(holds_alternative<ValueHolder::ArraY>(arrayVal->data)){

			auto &arr = get<ValueHolder::ArraY>(arrayVal->data);

			if (index < 0 || index >= static_cast<int>(arr.size())) {
				cerr << "ไม่สามารถลบ ดัชนี ที่อยู่นอกขอบเขต ชุดข้อมูล ได้ "
					 << "ดัชนี: " << index << ", ขนาด ชุดข้อมูล: " << arr.size()
					 << " ที่บรรทัด: " << stmt["line"] << " คอลัมน์: " << stmt["column"]
					 << "";
				exit(1);
			}

			arr.erase(arr.begin() + index);
		}else if(holds_alternative<string>(arrayVal->data)){
			auto &arr = get<string>(arrayVal->data);

			if (index < 0 || index >= static_cast<int>(arr.size())) {
				cerr << "ไม่สามารถลบ ดัชนี ที่อยู่นอกขอบเขต ชุดข้อมูล ได้ "
					 << "ดัชนี: " << index << ", ขนาด ชุดข้อมูล: " << arr.size()
					 << " ที่บรรทัด: " << stmt["line"] << " คอลัมน์: " << stmt["column"]
					 << "";
				exit(1);
			}

			arr.erase(index,1);
		}else{
			cerr << "ไม่สามารถลบสมาชิกได้ เนื่องจากไม่ใช่ชุดข้อมูล หรือ ข้อความ"
				 << " ที่บรรทัด: " << stmt["line"] << " คอลัมน์: " << stmt["column"]
				 << "";
			exit(1);
		}

		return nullptr;
	}
 else if (type == "ExitProcess") {
		exit(0);
	}else if (stmt["type"] == "export") {
	    for (const auto &func : stmt["function"]) {
	        functionDef def;


	        vector<string> parameterName;
	        for (const auto &a : func["parameter"]) {  // ✅ เปลี่ยนตรงนี้
	            parameterName.push_back(a["variable"]["name"]);
	        }
	        def.name = func["name"];
	        def.parameter = parameterName;
	        def.body = func["body"]["statements"];
	        exportedFunctions[func["name"]] = def;
			functionTable[func["name"]] = def;
	    }
	    return nullptr;
	}
	else if (stmt["type"] == "import") {
	    using namespace std;
	    namespace fs = std::filesystem;

	    string filename = stmt["file"];
	    string namespaceName = stmt["name"];

	    // ดึง path ของไฟล์แม่ ถ้ามี (จากการ import ซ้อน)
	    string currentFilePath = stmt.value("__currentFilePath", "");

	    // ถ้ามี path ของไฟล์แม่ → คำนวณ path ของไฟล์ลูกจากตรงนั้น
	    fs::path filePath;
	    if (!currentFilePath.empty()) {
	        fs::path parentPath = fs::path(currentFilePath).parent_path();
	        filePath = fs::absolute(parentPath / filename);
	    } else {
	        filePath = fs::absolute(filename);
	    }

	    if (!fs::exists(filePath)) {
	        cerr << "ไม่พบไฟล์ '" << filePath << "' ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
	        exit(1);
	    }

	    ifstream inFile(filePath);
	    if (!inFile.is_open()) {
	        cerr << "ไม่สามารถเปิดไฟล์ '" << filePath << "' ได้ ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
	        exit(1);
	    }

	    stringstream buffer;
	    buffer << inFile.rdbuf();
	    inFile.close();
	    string content = buffer.str();

	    if (content.empty()) {
	        cerr << "ไฟล์ '" << filePath << "' ว่างเปล่า! ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
	        exit(1);
	    }

	    json importedAST;
	    try {

	        importedAST = json::parse(content);
	    } catch (const json::parse_error& e) {
	        cerr << "ไม่สามารถ parse ไฟล์ '" << filePath << "' เป็น JSON ได้: " << e.what() << " ที่บรรทัด " << stmt["line"] << " คอลัมน์ " << stmt["column"] << "";
	        exit(1);
	    }

	    // ตั้งค่า __currentFilePath ให้ทุก statement ใน AST ที่ import มา
	    for (auto& innerStmt : importedAST["statements"]) {
	        innerStmt["__currentFilePath"] = filePath.string(); // จำ path ไว้
	    }

	    evalProgram(importedAST);
	    importModules[namespaceName] = exportedFunctions;
	    exportedFunctions.clear();

	    return nullptr;
	}


else if(type == "Comment"){
		return nullptr;
	}else if (type == "FunctionCall") {
        return evalExpr(stmt);  // คืนค่าที่ evalExpr คืนกลับมาเลย
    }

	cerr << "ไม่รู้จักคำสั่งประเภทนี้ "<<" ที่บรรทัด "<<stmt["line"]<<" คอลัมน์ "<<stmt["column"]<< "";
	exit(1);
	// bracket below refer to evalStatement
}

// make AST



/*.*/
class VariableDeclaretionNode : public ASTNode {
public:
	ASTNodePtr varname;
	string datatype;
	string vicedatatype;
	ASTNodePtr value;

	VariableDeclaretionNode(const ASTNodePtr varname, const string &datatype,
							const string &vicedatatype, ASTNodePtr value,
							const Token &t) :
		varname(move(varname)),
		datatype(datatype),
		vicedatatype(vicedatatype),
		value(move(value)),
		ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"variableDeclaration\",\"variable\":" +
			   varname->print() + ",\"datatype\":\"" + datatype +
			   "\",\"vicedatatype\":" +
			   (vicedatatype.empty() ? "null" : "\"" + vicedatatype + "\"") +
			   ",\"value\":" + (value ? value->print() : "null") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class AssignmentNode : public ASTNode {
public:
	ASTNodePtr varname;
	ASTNodePtr value;

	AssignmentNode(ASTNodePtr varName, ASTNodePtr value, const Token &t) :
		varname(move(varName)), value(move(value)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"assignment\",\"variable\":" + varname->print() +
			   ",\"value\":" + (value ? value->print() : "null") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class StringNode : public ASTNode {

public:
	string value;

	StringNode(const string &val, const Token &t) :
		value(val), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"string\",\"value\":" + value +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class NullNode : public ASTNode {
public:
	NullNode(const Token &t) :
		ASTNode(t) {}
	string print() const override {
		// Always return valid JSON, no condition
		return "{\"type\":\"null\",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class IntNode : public ASTNode {

public:
	int value;

	IntNode(int val, const Token &t) :
		value(val), ASTNode(t) {}
	string print() const override {
		return "{\"type\":\"int\",\"value\":" + to_string(value) +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class FloatNode : public ASTNode {

public:
	double value;

	FloatNode(double val, const Token &t) :
		value(val), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"float\",\"value\":" + to_string(value) +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class BoolNode : public ASTNode {

public:
	bool value;

	BoolNode(bool val, const Token &t) :
		value(val), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"bool\",\"value\":" +
			   string(value ? "true" : "false") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class VariableNode : public ASTNode {
public:
	string name;
	VariableNode(const string &name, const Token &t) :
		name(name), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"variable\",\"name\":\"" + name + "\"," +
			   "\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class BinaryOPNode : public ASTNode {
public:
	string op;
	ASTNodePtr left, right;
	BinaryOPNode(const string &op, ASTNodePtr left, ASTNodePtr right,
				 const Token &t) :
		op(op), left(move(left)), right(move(right)), ASTNode(t) {}
	string print() const override {
		return "{\"type\":\"binaryOp\",\"Op\":\"" + op +
			   "\",\"left\":" + (left ? left->print() : "null") +
			   ",\"right\":" + (right ? right->print() : "null") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class UnaryOpNode : public ASTNode {
public:
	string op;
	ASTNodePtr operand;
	UnaryOpNode(const string &op, ASTNodePtr operand, const Token &t) :
		op(op), operand(move(operand)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"unaryOp\",\"operator\":\"" + op +
			   "\",\"operand\":" + (operand ? operand->print() : "null") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class logarithmNode : public ASTNode {
public:
	ASTNodePtr value;
	logarithmNode(ASTNodePtr value, const Token &t) :
		value(move(value)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"ln\",\"value\":" +
			   (value ? value->print() : "null") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class PrintNode : public ASTNode {
public:
	vector<ASTNodePtr> expression;
	PrintNode(vector<ASTNodePtr> expression, const Token &t) :
		expression(move(expression)), ASTNode(t) {}

	string print() const override {
		string expreSTR = "[";
		for (size_t i = 0; i < expression.size(); i++) {
			if (i > 0) {
				expreSTR += ",";
			}
			expreSTR += expression[i]->print();
		}
		expreSTR += "]";
		return "{\"type\":\"print\",\"expression\":" + expreSTR +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class FunctionDeclaretionNode : public ASTNode {
public:
	string funcName;
	string dataType;
	vector<ASTNodePtr> parasmeter;
	vector<ASTNodePtr> expression;
	FunctionDeclaretionNode(const string &funcName,
							vector<ASTNodePtr> parasmeter,
							vector<ASTNodePtr> expression,
							const string &dataType, const Token &t) :
		funcName(funcName),
		parasmeter(move(parasmeter)),
		expression(move(expression)),
		dataType(dataType),
		ASTNode(t) {}

	string print() const override {
		string express = "[";
		string parasm = "[";
		for (size_t i = 0; i < expression.size(); i++) {
			if (i > 0) {
				express += ",";
			}
			express += expression[i]->print();
		}
		for (size_t i = 0; i < parasmeter.size(); i++) {
			if (i > 0) {
				parasm += ",";
			}
			parasm += parasmeter[i]->print();
		}
		express += "]";
		parasm += "]";

		return "{\"type\":\"functionDeclaretion\",\"name\":\"" + funcName +
			   "\",\"returnType\":\"" + dataType +
			   "\",\"parameter\":" + parasm +
			   ",\"body\": {\"type\":\"block\",\"statements\": " + express +
			   "},\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ImportNode : public ASTNode {
public:
	string file;
	string name;
	ImportNode(const string &file, const string &name, const Token &t) :
		file(file), name(name), ASTNode(t) {}

	string print() const override {

		return "{\"type\":\"import\",\"file\":" + file + ",\"name\":\"" + name +
			   "\"," + "\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ExportNode : public ASTNode {
public:
	vector<ASTNodePtr> functions;

	ExportNode(const vector<ASTNodePtr> &funcs, const Token &t) :
		functions(funcs), ASTNode(t) {}

	string print() const override {
		string result = "{\"type\":\"export\",\"function\":[";
		for (size_t i = 0; i < functions.size(); ++i) {
			result += functions[i] ? functions[i]->print() : "null";
			if (i + 1 < functions.size())
				result += ",";
		}
		result += "],\"line\":" + to_string(token.line) +
				  ",\"column\":" + to_string(token.column) + "}";
		return result;
	}
};

class FunctionCallNode : public ASTNode {
public:
    ASTNodePtr namespaceName; // ชื่อ namespace (อาจเป็น nullptr)
    ASTNodePtr funcname;      // ชื่อฟังก์ชัน
    vector<ASTNodePtr> argument; // อาร์กิวเมนต์

    FunctionCallNode(ASTNodePtr namespaceName, ASTNodePtr funcname,
                     vector<ASTNodePtr> argument, const Token &t)
        : namespaceName(std::move(namespaceName)),
          funcname(std::move(funcname)),
          argument(std::move(argument)),
          ASTNode(t) {}

    string print() const override {
        string args = "[";
        for (size_t i = 0; i < argument.size(); i++) {
            if (i > 0) {
                args += ",";
            }
            args += argument[i]->print();
        }
        args += "]";

        string result = "{\"type\":\"FunctionCall\"";
        if (namespaceName != nullptr) {
            result += ",\"namespace\":" + namespaceName->print();
        }
        result += ",\"name\":" + funcname->print() + ",\"argument\":" + args +
                  ",\"line\":" + to_string(token.line) +
                  ",\"column\":" + to_string(token.column) + "}";
        return result;
    }
};
class ReturnNode : public ASTNode {
public:
	ASTNodePtr value;
	ReturnNode(ASTNodePtr value, const Token &t) :
		value(move(value)), ASTNode(t) {}
	string print() const override {
		return "{\"type\":\"return\",\"value\":" +
			   (value ? value->print() : "null") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class WhileNode : public ASTNode {
public:
	ASTNodePtr condition;
	vector<ASTNodePtr> expression;
	WhileNode(ASTNodePtr condition, vector<ASTNodePtr> expression,
			  const Token &t) :
		condition(move(condition)), expression(move(expression)), ASTNode(t) {}
	string print() const override {
		string body;
		body += "[";
		for (size_t i = 0; i < expression.size(); i++) {
			if (i > 0) {
				body += ",";
			}
			body += expression[i]->print();
		}
		body += "]";
		return "{\"type\":\"whileloop\",\"condition\":" +
			   (condition ? condition->print() : "null") +
			   ",\"body\":{\"type\":\"block\",\"statements\":" + body +
			   +"},\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class DoWhileNode : public ASTNode {
public:
	ASTNodePtr condition;
	vector<ASTNodePtr> expression;
	DoWhileNode(ASTNodePtr condition, vector<ASTNodePtr> expression,
				const Token &t) :
		condition(move(condition)), expression(move(expression)), ASTNode(t) {}
	string print() const override {
		string body;
		body += "[";
		for (size_t i = 0; i < expression.size(); i++) {
			if (i > 0) {
				body += ",";
			}
			body += expression[i]->print();
		}
		body += "]";
		return "{\"type\":\"dowhileloop\",\"body\": "
			   "{\"type\":\"block\",\"statements\":" +
			   body +
			   "},\"condition\":" + (condition ? condition->print() : "null") +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ForNode : public ASTNode {
public:
	ASTNodePtr initialization;
	ASTNodePtr condition;
	ASTNodePtr chagevalue;
	vector<ASTNodePtr> statement;
	ForNode(ASTNodePtr init, ASTNodePtr cond, ASTNodePtr chag,
			vector<ASTNodePtr> statement, const Token &t) :
		initialization(move(init)),
		condition(move(cond)),
		chagevalue(move(chag)),
		statement(move(statement)),
		ASTNode(t) {}

	string print() const override {
		string state; // state mean statement🚠
		state += "[";
		for (size_t i = 0; i < statement.size(); i++) {
			if (i > 0) {
				state += ",";
			}
			state += statement[i]->print();
		}
		state += "]";
		return "{\"type\":\"forloop\",\"initialization\":" +
			   (initialization ? initialization->print() : "null") +
			   ",\"condition\":" + (condition ? condition->print() : "null") +
			   ",\"changevalue\":" +
			   (chagevalue ? chagevalue->print() : "null") +
			   ",\"body\":{\"type\":\"block\",\"statements\":" + state +
			   "},\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class BreakNode : public ASTNode {
public:
	BreakNode(const Token &t) :
		ASTNode(t) {}
	string print() const override {
		return "{\"type\":\"Break\",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ContinueNode : public ASTNode {
public:
	ContinueNode(const Token &t) :
		ASTNode(t) {}
	string print() const override {
		return "{\"type\":\"Continue\",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ExitProcessNode : public ASTNode {
public:
	ExitProcessNode(const Token &t) :
		ASTNode(t) {}
	string print() const override {
		return "{\"type\":\"ExitProcess\",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class IfNode : public ASTNode {
public:
	ASTNodePtr condition;
	vector<ASTNodePtr> body;
	vector<ASTNodePtr> ELIF;
	ASTNodePtr ELSE;
	IfNode(ASTNodePtr cond, vector<ASTNodePtr> body, const Token &t,
		   vector<ASTNodePtr> ELIF, ASTNodePtr ELSE) :
		condition(move(cond)),
		body(move(body)),
		ASTNode(t),
		ELIF(move(ELIF)),
		ELSE(move(ELSE)) {}

	string print() const override {
		string statement = "[";
		for (size_t i = 0; i < body.size(); i++) {
			if (i > 0)
				statement += ",";
			statement += body[i]->print();
		}
		statement += "]";

		string list_of_ELIF = "[";
		for (size_t i = 0; i < ELIF.size(); i++) {
			if (i > 0)
				list_of_ELIF += ",";
			list_of_ELIF += ELIF[i]->print(); // แก้ ElIF -> ELIF
		}
		list_of_ELIF += "]";

		string else_str = ELSE ? ELSE->print() : "null";

		return "{\"type\":\"if\",\"condition\":" +
			   (condition ? condition->print() : "null") +
			   ",\"body\":{\"type\":\"block\",\"statements\":" + statement +
			   "}" + ",\"elif\":" + list_of_ELIF + ",\"else\":" + else_str +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ElifNode : public ASTNode {
public:
	ASTNodePtr condition;
	vector<ASTNodePtr> statement;
	ElifNode(ASTNodePtr cond, vector<ASTNodePtr> statement, const Token &t) :
		condition(move(cond)), statement(move(statement)), ASTNode(t) {}

	string print() const override {
		string body;
		body += "[";

		for (size_t i = 0; i < statement.size(); i++) {
			if (i > 0) {
				body += ",";
			}
			body += statement[i]->print();
		}
		body += "]";

		return "{\"type\":\"elif\",\"condition\":" +
			   (condition ? condition->print() : "null") +
			   ",\"body\":{\"type\":\"block\",\"statements\":" + body +
			   "},\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ElseNode : public ASTNode {
public:
	vector<ASTNodePtr> statement;
	ElseNode(vector<ASTNodePtr> statement, const Token &t) :
		statement(statement), ASTNode(t) {}

	string print() const override {
		string body;
		body += "[";
		for (size_t i = 0; i < statement.size(); i++) {
			if (i > 0) {
				body += ",";
			}
			body += statement[i]->print();
		}
		body += "]";

		return "{\"type\":\"else\",\"body\":{\"type\":\"block\","
			   "\"statements\":" +
			   body + "},\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ConvertNode : public ASTNode {
public:
	ASTNodePtr expression;
	string targetType;

	ConvertNode(ASTNodePtr expr, const string &type, const Token &t) :
		expression(move(expr)), targetType(type), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"Convert\",\"target\":\"" + targetType +
			   "\",\"expression\":" + expression->print() +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class LengthNode : public ASTNode {
public:
	ASTNodePtr target;
	LengthNode(ASTNodePtr target, const Token &t) :
		target(move(target)), ASTNode(t) {}
	string print() const override {
		return "{\"type\":\"Length\",\"target\":" + target->print() +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class PopNode : public ASTNode {
public:
	ASTNodePtr array;

	PopNode(ASTNodePtr arr, const Token &t) :
		array(move(arr)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"Pop\",\"array\":" + array->print() +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class PushNode : public ASTNode {
public:
	ASTNodePtr array;
	ASTNodePtr value;

	PushNode(ASTNodePtr arr, ASTNodePtr val, const Token &t) :
		array(move(arr)), value(move(val)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"Push\",\"array\":" + array->print() +
			   ",\"value\":" + value->print() +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};

class InsertNode : public ASTNode {
public:
	ASTNodePtr array;
	ASTNodePtr index;
	ASTNodePtr value;

	InsertNode(ASTNodePtr arr, ASTNodePtr idx, ASTNodePtr val, const Token &t) :
		array(move(arr)), index(move(idx)), value(move(val)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"Insert\",\"array\":" + array->print() +
			   ",\"index\":" + index->print() + ",\"value\":" + value->print() +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class EraseNode : public ASTNode {
public:
	ASTNodePtr array;
	ASTNodePtr index;

	EraseNode(ASTNodePtr arr, ASTNodePtr idx, const Token &t) :
		array(move(arr)), index(move(idx)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"Erase\",\"array\":" + array->print() +
			   ",\"index\":" + index->print() +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class InputNode : public ASTNode {
public:
	ASTNodePtr varname;
	InputNode(ASTNodePtr varname, const Token &t) :
		varname(move(varname)), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"input\",\"variable\":" + varname->print() +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
class ArrayLiterelNode : public ASTNode {
public:
	vector<ASTNodePtr> element;
	ArrayLiterelNode(vector<ASTNodePtr> element, const Token &t) :
		element(move(element)), ASTNode(t) {}
	string print() const override {
		string elementstr = "[";
		for (size_t i = 0; i < element.size(); i++) {
			if (i > 0) {
				elementstr += ",";
			}
			elementstr += element[i]->print();
		}
		elementstr += "]";
		return "{\"type\":\"ArrayLiterel\",\"element\":" + elementstr +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}
};
// 1. Array Declaration Node (Fixed)
class ArrayDeclarationNode : public ASTNode {
	string varname;
	vector<ASTNodePtr> elements;

public:
	ArrayDeclarationNode(const string &varname, vector<ASTNodePtr> elements,
						 const Token &t) :
		varname(varname), elements(move(elements)), ASTNode(t) {}

	string print() const override {
		std::ostringstream oss;
		oss << "{\"type\":\"ArrayDeclaration\",\"name\":\"" << varname
			<< "\",\"elements\":[";
		for (size_t i = 0; i < elements.size(); ++i) {
			if (i > 0)
				oss << ",";
			oss << elements[i]->print();
		}
		oss << ",\"line\":" << token.line << ",\"column\":" << token.column
			<< "]}";
		return oss.str();
	}
};

// 2. Array Assignment Node
class ArrayAssignmentNode : public ASTNode {
	ASTNodePtr array;
	ASTNodePtr index;
	ASTNodePtr value;

public:
	ArrayAssignmentNode(ASTNodePtr array, ASTNodePtr index, ASTNodePtr value,
						const Token &t) :
		array(move(array)),
		index(move(index)),
		value(move(value)),
		ASTNode(t) {}

	string print() const override {
		std::ostringstream oss;
		oss << "{\"type\":\"ArrayAssignment\",\"array\":" << array->print()
			<< ",\"index\":" << index->print()
			<< ",\"value\":" << value->print() << ",\"line\":" << token.line
			<< ",\"column\":" << token.column << "}";
		return oss.str();
	}
};

// 3. Array Access Node
class ArrayAccessNode : public ASTNode {
	ASTNodePtr array;
	ASTNodePtr index;

public:
	ArrayAccessNode(ASTNodePtr array, ASTNodePtr index, const Token &t) :
		array(move(array)), index(move(index)), ASTNode(t) {}

	string print() const override {
		std::ostringstream oss;
		oss << "{\"type\":\"ArrayAccess\",\"array\":" << array->print()
			<< ",\"index\":" << index->print() << ",\"line\":" << token.line
			<< ",\"column\":" << token.column << "}";
		return oss.str();
	}
};

// 4. Object Declaration Node (Fixed)
class ObjectDeclarationNode : public ASTNode {
	vector<pair<ASTNodePtr, ASTNodePtr>> entries;

public:
	ObjectDeclarationNode(vector<pair<ASTNodePtr, ASTNodePtr>> entries,
						  const Token &t) :
		entries(move(entries)), ASTNode(t) {}

	string print() const override {
		std::ostringstream oss;
		oss << "{\"type\":\"Object\",\"entries\":[";
		for (size_t i = 0; i < entries.size(); ++i) {
			if (i > 0)
				oss << ",";
			oss << "{\"key\":" << entries[i].first->print()
				<< ",\"value\":" << entries[i].second->print() << "}";
		}
		oss << ",\"line\":" << token.line << ",\"column\":" << token.column
			<< "]}";
		return oss.str();
	}
};
class ObjectLiteralNode : public ASTNode {
	std::vector<std::pair<ASTNodePtr, ASTNodePtr>> properties;

public:
	ObjectLiteralNode(std::vector<std::pair<ASTNodePtr, ASTNodePtr>> props,
					  const Token &t) :
		properties(std::move(props)), ASTNode(t) {}

	std::string print() const override {
		std::ostringstream oss;
		oss << "{\"type\":\"ObjectLiteral\",\"properties\":[";

		for (size_t i = 0; i < properties.size(); ++i) {
			if (i > 0)
				oss << ","; // Add comma between entries

			const auto &[key, value] = properties[i];

			// Validate key type (should be enforced by parser)
			if (dynamic_cast<StringNode *>(key.get()) == nullptr) {
				throw std::runtime_error("Object keys must be strings");
			}

			oss << "{\"key\":" << key->print()
				<< ",\"value\":" << value->print() << "}";
		}

		oss << "]" << ",\"line\":" << token.line
			<< ",\"column\":" << token.column << "}";
		return oss.str();
	}
};
// 5. Object Assignment Node (Improved)
class ObjectAssignmentNode : public ASTNode {
	ASTNodePtr object;
	ASTNodePtr key;
	ASTNodePtr value;

public:
	ObjectAssignmentNode(ASTNodePtr object, ASTNodePtr key, ASTNodePtr value,
						 const Token &t) :
		object(move(object)), key(move(key)), value(move(value)), ASTNode(t) {}

	string print() const override {
		std::ostringstream oss;
		oss << "{\"type\":\"ObjectAssignment\",\"object\":" << object->print()
			<< ",\"key\":" << key->print() << ",\"value\":" << value->print()
			<< ",\"line\":" << token.line << ",\"column\":" << token.column
			<< "}";
		return oss.str();
	}
};

// 6. Object Access Node (Fixed parameter order)
class ObjectAccessNode : public ASTNode {
public:
	ASTNodePtr object;
	ASTNodePtr key;
	ObjectAccessNode(ASTNodePtr object, ASTNodePtr key, const Token &t) :
		object(move(object)), key(move(key)), ASTNode(t) {}
    ASTNodePtr getObject() const { return object; }
    ASTNodePtr getMember() const { return key; }
	string print() const override {
		std::ostringstream oss;
		oss << "{\"type\":\"ObjectAccess\",\"object\":" << object->print()
			<< ",\"key\":" << key->print() << ",\"line\":" << token.line
			<< ",\"column\":" << token.column << "}";
		return oss.str();
	}
};
// cut string

class CommentNode : public ASTNode {
public:
	string text;
	CommentNode(const string &text, const Token &t) :
		text(text), ASTNode(t) {}

	string print() const override {
		return "{\"type\":\"Comment\",\"text\":\"" + escapeString(text) + "\"" +
			   ",\"line\":" + to_string(token.line) +
			   ",\"column\":" + to_string(token.column) + "}";
	}

private:
	string escapeString(const string &s) const {
		// Implement string escaping for JSON
		string result;
		for (char c : s) {
			if (c == '"')
				result += "\\\"";
			else if (c == '\\')
				result += "\\\\";
			else
				result += c;
		}
		return result;
	}
};
class ProgramNode : public ASTNode {
public:
	vector<ASTNodePtr> statements;
	ProgramNode(vector<ASTNodePtr> statements, const Token &t) :
		statements(move(statements)), ASTNode(t) {}

	string print() const override {
		string stmts = "[";
		for (size_t i = 0; i < statements.size(); i++) {
			if (i > 0)
				stmts += ",";
			stmts += statements[i]->print();
		}
		stmts += "]";
		return "{\"type\":\"Program\",\"statements\":" + stmts +
			   ",\"line\": 0,\"column\":0}";
	}
};
class EmptyStatementNode : public ASTNode {
public:
	string print() const override { return "{\"type\":\"EmptyStatement\"}"; }
};
class Parser {
	vector<Token> tokens;
	size_t pos = 0;

public:
	Parser(const vector<Token> &t) :
		tokens(t) {}
	Token peek() {
		if (pos < tokens.size()) {
			return tokens[pos];
		}
		return {"EOF", "", 0, 0};
	}
	Token advance() {

		if (pos < tokens.size()) {
			Token tok = tokens[pos];

			pos++;
			return tok;
		} else {

			return Token{"EOF", "", 0, 0};
		}

	}
	bool match(const string &type) {
		if (peek().type == type) {
			advance();
			return true;
		}

		return false;
	}
	ASTNodePtr parse() {
		Token t = peek();
		vector<ASTNodePtr> statements;
		while (peek().type != "EOF") {
			statements.push_back(parseStatement());
		}
		return make_shared<ProgramNode>(statements, t);
	}
	ASTNodePtr parseExpression() { return parseAssigment(); }
	ASTNodePtr parseAssigment() {

		ASTNodePtr left = parseLogicalOR();
		Token t = peek();
		if (match("EQUALSSIGN")) {
			ASTNodePtr right = parseExpression();
			return make_shared<AssignmentNode>(left, right, t);
		}

		return left;
	}
	bool isLvalue(const ASTNodePtr &node) {
		return dynamic_cast<VariableNode *>(node.get()) != nullptr ||
			   dynamic_cast<ArrayAccessNode *>(node.get()) != nullptr ||
			   dynamic_cast<ObjectAccessNode *>(node.get()) != nullptr;
	}
	ASTNodePtr parseLogicalOR() {

		ASTNodePtr left = parseLogicalAND();
		while (peek().type =="OR")
		{
			Token t = peek();
			string op = advance().type;
			ASTNodePtr right = parseLogicalAND();
			left = make_shared<BinaryOPNode>(op, left, right, t);
		}
		return left;
	}

	ASTNodePtr parseLogicalAND() {

		ASTNodePtr left = parseLogicalBiswiseOR();
		while (peek().type ==
			   "AND") // match("AND", "AND in parseLOgicalAND")
		{
			Token t = peek();
			string op = advance().type;
			ASTNodePtr right = parseLogicalBiswiseOR();
			left = make_shared<BinaryOPNode>(op, left, right, t);
		}
		return left;
	}

	ASTNodePtr parseLogicalBiswiseOR() {

		ASTNodePtr left = parseLogicalBiswiseXOR();
		while (peek().type ==
			   "BITWISE_OR") // match("BITWISE_OR", "BiswiseOR in
							 // parseLogicalBiswiseOR")
		{
			Token t = peek();
			string op = advance().type;
			ASTNodePtr right = parseLogicalBiswiseXOR();
			left = make_shared<BinaryOPNode>(op, left, right, t);
		}
		return left;
	}
	ASTNodePtr parseLogicalBiswiseXOR() {

		ASTNodePtr left = parseLogicalBiswiseAND();
		while (peek().type ==
			   "XOR") // match("BITWISE_XOR", "BiswiseXOR in
					  // parseLogicalBiswiseXOR")
		{
			Token t = peek();
			string op = advance().type;
			ASTNodePtr right = parseLogicalBiswiseAND();
			left = make_shared<BinaryOPNode>(op, left, right, t);
		}
		return left;
	}
	ASTNodePtr parseLogicalBiswiseAND() {

		ASTNodePtr left = parseEquality();
		while (peek().type == "BITWISE_AND")
		{
			Token t = peek();
			string op = advance().type;
			ASTNodePtr right = parseEquality();
			left = make_shared<BinaryOPNode>(op, left, right, t);
		}
		return left;
	}
	ASTNodePtr parseEquality() {

		ASTNodePtr left = parseRelational();
		while (true) {
			Token t = peek();
			if (peek().type == "EQUALTO" ||
				peek().type == "NOTEQUAL") {
				string op = advance().type;
				ASTNodePtr right = parseRelational();
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else {
				break;
			}
		}
		return left;
	}
	ASTNodePtr parseRelational() {
		ASTNodePtr left = parseShift();
		while (true) {
			Token t = peek();
			if (peek().type == "GREATER" ||
				peek().type == "LESSER" ||
				peek().type ==
					"GREATEROREQUAL" ||
				peek().type ==
					"LESSEROREQUAL") {
				string op =
					advance().type;
				ASTNodePtr right = parseShift();
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else {
				break;
			}
		}
		return left;
	}
	ASTNodePtr parseShift() {
		ASTNodePtr left = parseAdditive();
		while (true) {
			Token t = peek();
			if (peek().type == "SHIFT_LEFT" ||
				peek().type == "SHIFT_RIGHT")
			{
				string op = advance().type;
				ASTNodePtr right = parseAdditive();
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else {
				break;
			}
		}
		return left;
	}
	ASTNodePtr parseAdditive() {
		ASTNodePtr left = Multiplicative();
		while (true) {
			Token t = peek();
			if (peek().type == "ADDITION" ||
				peek().type == "SUBTRACTION") 
			{
				string op = advance().type;
				ASTNodePtr right = Multiplicative();
				left = make_shared<BinaryOPNode>(op, left, right, t);

			} else
				break;
		}
		return left;
	}
	ASTNodePtr Multiplicative() {
		ASTNodePtr left = parseExponents();
		while (true) {
			Token t = peek();
			if (peek().type == "MULTIPLICATION") 
			{
				string op =
					advance().type;
				ASTNodePtr right = parseExponents();
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else if (peek().type == "FLOORDIVISION")
			{
				string op = advance().type;

				ASTNodePtr right = parseExponents();
				if (auto intNode = dynamic_cast<IntNode *>(right.get())) {
					if (intNode->value == 0) {
						throw runtime_error("ห้ามหารเอาส่วนด้วยศูนย์");
					}
				}
				if (auto floatNode = dynamic_cast<FloatNode *>(right.get())) {
					if (floatNode->value == 0) {
						throw runtime_error("ห้ามหารเอาส่วนด้วยศูนย์");
					}
				}
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else if (peek().type == "DIVISION")
			{
				string op =
					advance().type;

				ASTNodePtr right = parseExponents();
				if (auto intNode = dynamic_cast<IntNode *>(right.get())) {
					if (intNode->value == 0) {
						throw runtime_error("ห้ามหารด้วยศูนย์");
					}
				}
				if (auto floatNode = dynamic_cast<FloatNode *>(right.get())) {
					if (floatNode->value == 0) {
						throw runtime_error("ห้ามหารด้วยศูนย์");
					}
				}
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else if (peek().type =="MODULAS") 
			{
				string op =advance().type;

				ASTNodePtr right = parseExponents();
				if (auto intNode = dynamic_cast<IntNode *>(right.get())) {
					if (intNode->value == 0) {
						throw runtime_error("ห้ามMODด้วยศูนย์");
					}
				}
				if (auto floatNode = dynamic_cast<FloatNode *>(right.get())) {
					if (floatNode->value == 0) {
						throw runtime_error("ห้ามMODด้วยศูนย์");
					}
				}
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else {
				break;
			}
		}
		return left;
	}
	ASTNodePtr parseExponents() {
		ASTNodePtr left = parseln();
		while (true) {
			Token t = peek();
			if (peek().type =="EXPONENTIATION" ||
				peek().type == "ROOT") {
				string op = advance().type;
				ASTNodePtr right = parseln();
				left = make_shared<BinaryOPNode>(op, left, right, t);
			} else {
				break;
			}
		}
		return left;
	}
	ASTNodePtr parseln() {

		if (peek().type == "LN") {
			Token t = peek();
			advance();
			if (!match("OPEN_PAREN")) {
				syntaxError(peek(), "ขาด วงเล็บเปิด ตรง ln");
			}
			ASTNodePtr val = parseExpression();
			if (!match("CLOSE_PAREN")) {
				syntaxError(peek(), "ขาด วงเล็บปิด ตรง ln");
			}
			return make_shared<logarithmNode>(val, t);
		}
		return parseUnary();
	}
	ASTNodePtr parseUnary() {
		// Prefix unary operators
		if (peek().type == "NOT" ||
			peek().type == "BITWISE_NOT" ||
			peek().type == "INCREMENT" ||
			peek().type == "DECREMENT"||
			peek().type == "SUBTRACTION") {
			Token t = peek();
			string op = advance().type;
			ASTNodePtr operand = parseUnary(); // Recursively parse operand
			return make_shared<UnaryOpNode>(op, operand, t);
		}

		// Parse primary expression
		ASTNodePtr node = parsepimary();

		// Postfix unary operators (e.g., x++, x--)
		if (peek().type == "INCREMENT" ||
			peek().type == "DECREMENT") {
			Token t = peek();
			string op = advance().type;
			return make_shared<UnaryOpNode>(
				op, node, t); // Or a different node type like PostfixOpNode
		}

		return node;
	}

	ASTNodePtr parsepimary() {

		if (peek().type =="INTEGER_VALUE") {
			return parseIntLiteral();
		} else if (peek().type =="FLOAT_VALUE") {
			return parseFloatLiteral();
		} else if (peek().type =="STRING_VALUE") {
			return parseStringLiteral();
		} else if (peek().type =="TRUE_VALUE" ||
				   peek().type =="FALSE_VALUE") {
			return parseBooliteral();
		} else if (peek().type =="BRACKET_OPENING") {
			return parseArrayLiteral();
		} else if (peek().type =="OPEN_BRACKETS") {
			return parseObectLiteral();
		} else if (peek().type =="OPEN_PAREN") {
			advance();
			ASTNodePtr exps = parseExpression();
			if (!match("CLOSE_PAREN")) {
				syntaxError(peek(),"ขาด วงเล็บปิด");
			}
			return exps;
		} else  if (peek().type == "IDENTIFIER") {
	        ASTNodePtr base = parsevariable(); // Parse initial identifier

	        // Handle chained accesses (array, object, function call)
	        while (true) {
	            Token t = peek();

	            // 1. Array access: base[...]
	            if (peek().type == "BRACKET_OPENING") {
	                advance();
	                ASTNodePtr index = parseExpression();
	                if (!match("BRACKET_CLOSEING")) {
	                    syntaxError(peek(), "ขาด ] ในการเข้าถึง ชุดข้อมูล");
	                }
	                base = make_shared<ArrayAccessNode>(base, index, t);
	            }
	            // 2. Object access: base.property or base.method
	            else if (peek().type == "DOT") {
	                advance();

	                // Handle built-in properties (like .length)
	                if (peek().type == "LENGTH") {
	                    advance();
	                    if (!match("OPEN_PAREN")) {
	                        syntaxError(peek(), "ขนาดต้องการ ()");
	                    }
	                    if (!match("CLOSE_PAREN")) {
	                        syntaxError(peek(),"ขนาด ต้องการ )");
	                    }
	                    base = make_shared<LengthNode>(base, t);
	                }
		            else if (peek().type == "PUSH" ||
		                     peek().type == "POP" ||
		                     peek().type == "INSERT" ||
		                     peek().type == "ERASE")
		            {
		                string method = advance().type;
		                if (!match("OPEN_PAREN")) {
		                    syntaxError(peek(), "ขาด ( หลัง " + method);
		                }

		                if (method == "POP" || method == "ERASE") {
		                    // POP and ERASE take one argument

		                    if (method == "POP") {
		                        base = make_shared<PopNode>(base, t);
		                    } else {
			                    ASTNodePtr arg = parseExpression();
		                        base = make_shared<EraseNode>(base, arg, t);
		                    }
		                    if (!match("CLOSE_PAREN")) {
		                        syntaxError(peek(), "ขาด ) หลัง " + method + " อากิวเมนต์");
		                    }
		                }
		                else { // PUSH or INSERT
		                    // First argument
		                    ASTNodePtr arg1 = parseExpression();

		                    if (method == "PUSH") {
		                        if (!match("CLOSE_PAREN")) {
		                            syntaxError(peek(), "ขาด ) หลัง " + method + " อากิวเมนต์");
		                        }
		                        base = make_shared<PushNode>(base, arg1, t);
		                    }
		                    else { // INSERT
		                        if (!match("COMMA")) {
		                            syntaxError(peek(), "ขาด , ในการ แทรก");
		                        }

		                        ASTNodePtr arg2 = parseExpression();
		                        if (!match("CLOSE_PAREN")) {
		                            syntaxError(peek(), "ขาด ) หลังการ แทรก");
		                        }
		                        base = make_shared<InsertNode>(base, arg1, arg2, t);
		                    }
		                }


		            }
	                // Regular member access
	                else if (peek().type == "IDENTIFIER") {
	                    ASTNodePtr member = parsevariable();
	                    base = make_shared<ObjectAccessNode>(base, member, t);
	                }
	                else {
	                    syntaxError(peek(), "ขาดชื่อคีย์ หลัง . ในการเข้าถึง ออบเจ็กต์");
	                }
	            }
	            // 3. Function call: base(...)
	            else if (peek().type == "OPEN_PAREN") {
	                // เก็บ base ปัจจุบันไว้ชั่วคราว
	                ASTNodePtr potentialNamespace = base;
	                ASTNodePtr functionName = nullptr;

	                // ตรวจสอบว่า base เป็น ObjectAccessNode หรือไม่ (namespace.function)
	                if (auto objAccess = dynamic_cast<ObjectAccessNode*>(potentialNamespace.get())) {
	                    // ใช้ getter ที่ถูกต้อง
	                    potentialNamespace = objAccess->getObject();
	                    functionName = objAccess->getMember();
	                }
	                else {
	                    // ถ้าไม่ใช่ ObjectAccessNode ให้ใช้ base เป็น function name โดยไม่มี namespace
	                    functionName = base;
	                    potentialNamespace = nullptr; // ไม่มี namespace
	                }

	                Token parenToken = advance();
	                vector<ASTNodePtr> args;

	                // Parse arguments
	                while (peek().type != "CLOSE_PAREN" &&
	                       peek().type != "EOF")
	                {
	                    args.push_back(parseExpression());
	                    if (match("COMMA")) {
	                        continue;
	                    } else {
	                        break;
	                    }
	                }

	                if (!match("CLOSE_PAREN")) {
	                    syntaxError(peek(), "ขาด ) หลังการเรียก โปรแกรม");
	                }

	                // ตรวจสอบประเภทข้อมูลก่อนสร้าง FunctionCallNode
	                if (!functionName) {
	                    syntaxError(parenToken, "ชื่อฟังก์ชันไม่ถูกต้อง");
	                    return nullptr;
	                }

	                // สร้าง FunctionCallNode ใหม่โดยส่ง ASTNodePtr ที่ถูกต้อง
	                base = make_shared<FunctionCallNode>(
	                    potentialNamespace,
	                    functionName,
	                    args,
	                    parenToken
	                );
	            }
	            // 4. Special methods (.push, .pop, etc.)

	            // 5. No more chaining
	            else {
	                break;
	            }


	        }

	        return base;
	    }
		 else if (peek().type =="CONVERTDATATYPE") {
			return parseConvertdataType();
		} else if (peek().type == "NULL") {
			return parseNullLiteral();
		}

		stringstream ss;
		ss << "คาดหวังนิพจน์แต่พบ '" << peek().value
		   << "' (ชนิด: " << peek().type << ")";
		syntaxError(peek(), ss.str());
		return nullptr;
	}
	ASTNodePtr parseArrayOperation() {
		ASTNodePtr array = parsepimary();
		return array;
	}
	ASTNodePtr parseConvertdataType() {
		Token t = peek();
		if (!match("CONVERTDATATYPE")) {
			throw runtime_error("การเปลี่ยนชนิดข้อมูลผิดพลาด");
		}
		if (!match("OPEN_PAREN")) {
			syntaxError(peek(),"ต้องมี ( หลัง เปลี่ยนชนิดข้อมูล");
		}
		ASTNodePtr expr = parseExpression();
		if (!match("COMMA")) {
			syntaxError(peek(),"ขาด , ที่ เปลี่ยนชนิดข้อมูล");
		}
		static const set<string> validTypes = {"INTEGER", "FLOAT", "STRING",
											   "BOOLLEAN"};

		Token typeToken = peek();
		if (!validTypes.count(typeToken.type)) {
			throw runtime_error("ชนิดข้อมูลไม่ถูกต้อง: " + typeToken.value +"การเปลี่ยนชนิดข้อมูลรอบรับแค่ จำนวนเต็ม ทศนิยม ข้อความ และ ค่าความจริง");
		}
		advance(); // Consume the
																// type token

		if (!match("CLOSE_PAREN")) {
			syntaxError(
				peek(),"ข้อผิดพลาด: ต้องการ ) ปิดการเปลี่ยนชนิดข้อมูล");
		}

		return make_shared<ConvertNode>(expr, typeToken.type, t);
	}
	ASTNodePtr parseObjectAccess() {
		auto object = parsepimary(); // Could be a variable or nested access
		Token t = peek();
		if (!match("DOT")) {
			syntaxError(peek(),"การเข้าถึง อ็อบเจกต์ต้องใช้ .");
		}

		if (peek().type != "STRING_VALUE") {
			syntaxError(peek(),"คีย์ต้องเป็น ข้อความ");
		}
		auto key = parseStringLiteral();

		return make_shared<ObjectAccessNode>(object, key, t);
	}

	ASTNodePtr parseArrayAccess() {
		auto array = parsepimary(); // Could be a variable or nested access
		Token t = peek();
		if (!match("BRACKET_OPENING")) {
			syntaxError(peek(),"การเข้าถึง ชุดข้อมูล ต้องใช้ [");
		}
		auto index = parseExpression();
		if (!match("BRACKET_CLOSEING")) {
			syntaxError(peek(), "ขาด ]");
		}

		return make_shared<ArrayAccessNode>(array, index, t);
	}

	string Datatype(string datatype) {
		if (datatype == "INTEGER") {
			return "int";
		} else if (datatype == "FLOAT") {
			return "float";
		} else if (datatype == "STRING") {
			return "string";
		} else if (datatype == "BOOLLEAN") {
			return "bool";
		} else if (datatype == "ARRAY") {
			return "array";
		} else if (datatype == "OBJECT") {
			return "object";
		} else {
			return "No match datatype";
		}
	}

	ASTNodePtr parseVariableDecleartion() {
		Token t = peek();
		if (!match("DECLARE")) {
			syntaxError(peek(),"ขาด ให้ ในการกำหนดตัวแปร");
		}
		if (peek().type !="IDENTIFIER") {
			syntaxError(peek(),"ไม่มีชื่อตัวแปร");
		}
		ASTNodePtr varname = parsevariable();
		ASTNodePtr initialValue = nullptr;
		string modifier = "";
		if (peek().type =="IDENTIFIER") {
			syntaxError(peek(),"ไม่มีคำสั่ง");
		}
		if (peek().type == "BE") {
			advance();
			if (match("CONST")) {
				modifier = "const";
			}

			else {
				syntaxError(
					peek(),"ไม่มีmodifierชื่อนี้");
			}
		}
		if (peek().type =="EQUALSSIGN") {
			advance();
			initialValue = parseExpression();
		}

		if (modifier == "const" && !initialValue) {
			syntaxError(peek(),"ค่าคงที่ต้องกำหนดค่าเริ่มต้น");
		}
		if (!match("SEMICOLON")) {
			syntaxError(peek(),"ต้องมี ; หลังจบคำสั่ง");
		}
		return make_shared<VariableDeclaretionNode>(varname, "have no", modifier,
													initialValue, t);
	}

	ASTNodePtr parseArrayLiteral() {
		Token t = peek();
		if (!match("BRACKET_OPENING")) {
			syntaxError(peek(), "ขาดเครื่องหมาย [ ใน ชุดข้อมูล");
		}
		vector<ASTNodePtr> element;
		while (peek().type != "BRACKET_CLOSEING" &&
			   peek().type != "EOF") {
			element.push_back(parseArrayElement());
			if (!match("COMMA")) {
				break;
			}
		}
		if (!match("BRACKET_CLOSEING")) {
			syntaxError(peek(), "ขาดเครื่องหมาย ] ในชุดข้อมูล");
		}

		return make_shared<ArrayLiterelNode>(element, t);
	}

	ASTNodePtr parseArrayElement() {
		ASTNodePtr element = parseExpression();
		if (!isValidArrayElement(element)) {
			throw runtime_error("สมาชิคใน ชุดข้อมูล ผิดพลาด");
		}
		return element;
	}
	bool isValidArrayElement(ASTNodePtr node) {
		// Whitelist allowed types
		return dynamic_cast<StringNode *>(node.get()) ||
			   dynamic_cast<IntNode *>(node.get()) ||
			   dynamic_cast<FloatNode *>(node.get()) ||
			   dynamic_cast<BoolNode *>(node.get()) ||
			   dynamic_cast<ArrayLiterelNode *>(node.get()) ||
			   dynamic_cast<ObjectLiteralNode *>(node.get()) ||
			   dynamic_cast<FunctionCallNode *>(node.get()) ||
			   dynamic_cast<VariableNode *>(node.get()) ||
			   dynamic_cast<NullNode *>(node.get());
	}
	ASTNodePtr parseObectLiteral() {
		Token t = peek();
		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(),
						"ขาดเครื่องหมาย { ใน อ็อบเจกต์");
		}
		vector<pair<ASTNodePtr, ASTNodePtr>> entrity;
		while (peek().type != "CLOSE_BRACKETS" &&
			   peek().type != "EOF") {
			auto key = parseObjectKey();
			if (!match("COLON")) {
				syntaxError(peek(),"ขาดเครื่องหมาย : ใน อ็อบเจกต์");
			}
			auto value = parseArrayElement();
			entrity.emplace_back(key, value);
			if (!match("COMMA")) {
				break;
			}
		}
		if (!match("CLOSE_BRACKETS")) {
			syntaxError(peek(),"ขาดเครื่องหมาย } ใน อ็อบเจกต์");
		}
		return make_shared<ObjectLiteralNode>(entrity, t);
	}
	ASTNodePtr parseObjectKey() {
		if (peek().type != "STRING_VALUE") {
			syntaxError(peek(), "คีย์ต้องเป็น ข้อความ");
		}
		return parseStringLiteral();
	}
	ASTNodePtr parseStringLiteral() {
		Token t = peek();
		if (peek().type !="STRING_VALUE") { // Add validation
			syntaxError(peek(), "ขาด ข้อความ");
		}
		Token token = advance();
		return make_shared<StringNode>(token.value, t);
	}
	ASTNodePtr parseNullLiteral() {
		Token t = peek();
		if (peek().type != "NULL") { // Add validation
			syntaxError(peek(), "ขาด ว่าง");
		}
		Token token = advance();
		return make_shared<NullNode>(t);
	}
	ASTNodePtr parseIntLiteral() {
		Token t = peek();
		Token token = advance();
		return make_shared<IntNode>(stoi(token.value), t);
	}

	ASTNodePtr parseFloatLiteral() {
		Token t = peek();
		Token token = advance();
		return make_shared<FloatNode>(stod(token.value), t);
	}

	ASTNodePtr parseBooliteral() {
		Token t = peek();
		Token token = advance();
		if (token.type != "TRUE_VALUE" && token.type != "FALSE_VALUE") {
			syntaxError(peek(),
						"ไม่มีค่าความจริง ชื่อ: " + token.type);
		}
		bool Boovalue = (token.type == "TRUE_VALUE" ? true : false);
		return make_shared<BoolNode>(Boovalue, t);
	}


	ASTNodePtr parsevariable() {
		Token t = peek();
		string varname = advance().value;
		return make_shared<VariableNode>(varname, t);
	}

	ASTNodePtr parsePrint() {
		if (match("PRINT")) {
			Token t = peek();
			if (match("OPEN_PAREN")) {
				vector<ASTNodePtr> expressions;

				// Parse first expression
				expressions.push_back(parseExpression());

				// Parse additional expressions
				while (match("COMMA")) {
					expressions.push_back(parseExpression());
				}

				if (!match("CLOSE_PAREN"))
					syntaxError(peek(), "ที่ แสดงผล ขาด )");
				if (!match("SEMICOLON"))
					syntaxError(peek(), "ที่ แสดงผล ขาด ;");

				return make_shared<PrintNode>(expressions, t);
			}
		}
		throw runtime_error("แสดงผล ไม่ถูกต้อง");
	}
	ASTNodePtr parseForLoop() {
		Token t = peek();

		if (!match("FOR")) {
			syntaxError(peek(), "ขาด คำสั่ง ทำซ้ำ");
		}

		if (!match("OPEN_PAREN")) {
			syntaxError(peek(), "ขาด ( ที่ ทำซ้ำ");
		}
		if (!match("STATEMENT1")) {
			syntaxError(peek(),"ขาด ตั้งแต่ ก่อนกำหนดค่าเริ่มต้น ที่ ทำซ้ำ");
		}
		ASTNodePtr init = nullptr; // init mean initialization😎
		if (peek().type == "DECLARE") {
			Token t = peek();
					if (!match("DECLARE")) {
						syntaxError(peek(),"ขาด ให้ ในการกำหนดตัวแปร");
					}

					if (peek().type !="IDENTIFIER") {
						syntaxError(peek(),"ไม่มีชื่อตัวแปร");
					}
					ASTNodePtr varname = parsevariable();
					ASTNodePtr initialValue = nullptr;
					string modifier = "";
					if (peek().type =="IDENTIFIER") {
						syntaxError(peek(),"ไม่มีคำสั่ง");
					}
					if (peek().type == "BE") {
						advance();
						if (match("CONST")) {
							modifier = "const";
						}

						else {
							syntaxError(
								peek(),"ไม่มีmodifierชื่อนี้");
						}
					}
					if (peek().type =="EQUALSSIGN") {
						advance();
						initialValue = parseExpression();
					}

					if (modifier == "const" && !initialValue) {
						syntaxError(peek(),"ค่าคงที่ต้องกำหนดค่าเริ่มต้น");
					}
					init =  make_shared<VariableDeclaretionNode>(varname, "have no", modifier,
																initialValue, t);
		} else if (peek().type != "STATEMENT2") {
			init = parseExpression();
		}
		if (!match("STATEMENT2")) {
			syntaxError(peek(), "ขาด จนถึง ตามหลังค่าเริ่มต้น ที่ ทำซ้ำ");
		}
		ASTNodePtr cond = nullptr; // cond mean condition¯\_(ツ)_/¯
		if (peek().type != "STATEMENT3") {
			cond = parseExpression();
		}
		if (!match("STATEMENT3")) {
			syntaxError(peek(),
						"ขาด โดยแต่ละรอบ ตามหลังค่าเงื่อนไข ที่ ทำซ้ำ");
		}
		ASTNodePtr inc = nullptr; // inc mean incrasement 👌
		if (peek().type != "CLOSE_PAREN") {
			inc = parseExpression();
		}
		if (!match("CLOSE_PAREN")) {
			syntaxError(peek(),
						"ขาด ) ตามหลังการเปลี่ยนค่า ที่ ทำซ้ำ");
		}
		vector<ASTNodePtr> body;
		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(), "ขาด { ที่ ทำซ้ำ");
		}
		while (peek().type != "CLOSE_BRACKETS" &&
			   peek().type !="EOF")
		{
			body.push_back(parseStatement());
		}

		if (!match("CLOSE_BRACKETS")) {
			syntaxError(peek(), "ขาด } ที่ ทำซ้ำ");
		}
		return make_shared<ForNode>(init, cond, inc, body, t);
	}
	ASTNodePtr parseWhileloop() {
		Token t = peek();

		if (!match("WHILE")) {

			syntaxError(peek(), "ขาด คำสั่ง ขณะ");
		}


		if (!match("OPEN_PAREN")) {
			syntaxError(peek(), "ขาด ( ที่ ขณะ");
		}
		ASTNodePtr cond = nullptr; // cond mean condition o((>ω< ))o
		if (peek().type != "CLOSE_PAREN") {
			cond = parseExpression();
		}
		if (!match("CLOSE_PAREN")) {
			syntaxError(peek(), "ขาด ) ที่ ขณะ");
		}
		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(), "ขาด { ที่ ขณะ");
		}
		vector<ASTNodePtr> body;
		while (peek().type != "CLOSE_BRACKETS" &&
			   peek().type != "EOF")
		{
			body.push_back(parseStatement());
		}
		if (!match("CLOSE_BRACKETS")) {
			syntaxError(peek(), "ขาด } ที่ ขณะ");
		}
		return make_shared<WhileNode>(cond, body, t);
	}
	ASTNodePtr parseDoWhile() {
		Token t = peek();
		if (!match("DO")) {

			syntaxError(peek(), "ไม่พบ ทำ ใน ทำ..ขณะ");
		}

		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(), "ไม่พบ { ใน ทำ..ขณะ");

		}
		vector<ASTNodePtr> body;
		while (peek().type != "CLOSE_BRACKETS" &&
			   peek().type !="EOF")
		{
			body.push_back(parseStatement());
		}
		if (!match("CLOSE_BRACKETS")) {

			syntaxError(peek(), "ไม่พบ } ใน ทำ..ขณะ");
		}
		if (!match("WHILE")) {
			syntaxError(peek(), "ไม่พบ ขณะ ใน ทำ..ขณะ");
		}
		if (!match("OPEN_PAREN")) {
			syntaxError(peek(), "ไม่พบ ( ใน ทำ..ขณะ");

		}
		if (!match("WHILE")) {
			syntaxError(peek(), "ไม่พบ ขณะ ใน ทำ..ขณะ");
		}
		if (!match("OPEN_PAREN")) {
			syntaxError(peek(), "ไม่พบ ( ใน ทำ..ขณะ");

		}
		ASTNodePtr cond = nullptr;
		if (peek().type != "CLOSE_PAREN" &&
			peek().type != "EOF") {
			cond = parseExpression();
		}
		if (!match("CLOSE_PAREN")) {

			syntaxError(peek(), "ไม่พบ ) ใน ทำ..ขณะ");

		}
		return make_shared<DoWhileNode>(cond, body, t);
	}
	ASTNodePtr parseElif() {
		Token t = peek();
		if (!match("ELIF")) {
			syntaxError(peek(), "ไม่พบคำสั่ง มิฉะนั้นถ้า");
		}

		if (!match("OPEN_PAREN")) {
			syntaxError(peek(), "ต้องมี ( ก่อนเงื่อนไข ที่ มิฉะนั้นถ้า");
		}
		ASTNodePtr cond = nullptr;
		if (peek().type != "CLOSE_PAREN") {
			cond = parseExpression();
		}
		if (!match("CLOSE_PAREN")) {
			syntaxError(peek(), "ต้องมี ) หลังเงื่อนไข ที่ มิฉะนั้นถ้า");
		}
		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(), "ต้องมี { ก่อนคำสั่ง ที่ มิฉะนั้นถ้า");
		}
		vector<ASTNodePtr> body;
		while (
			peek().type != "CLOSE_BRACKETS" &&
			peek().type !="EOF")
		{
			body.push_back(parseStatement());
		}
		if (!match("CLOSE_BRACKETS")) {
			syntaxError(peek(), "ต้องมี } หลังคำสั่ง ที่ มิฉะนั้นถ้า");
		}
		return make_shared<ElifNode>(cond, body, t);
	}

	ASTNodePtr parseIf() {
		Token t = peek();
		if (!match("IF")) {
			syntaxError(peek(), "ไม่พบคำสั่ง ถ้า");
		}

		if (!match("OPEN_PAREN")) {
			syntaxError(peek(), "ต้องมี ( ก่อนเงื่อนไข ที่ ถ้า");
		}
		ASTNodePtr cond = nullptr;
		if (peek().type != "CLOSE_PAREN") {
			cond = parseExpression();
		}
		if (!match("CLOSE_PAREN")) {
			syntaxError(peek(), "ต้องมี ) หลังเงื่อนไข ที่ ถ้า");
		}
		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(), "ต้องมี { ก่อนคำสั่ง ที่ ถ้า");
		}
		vector<ASTNodePtr> body;
		while (
			peek().type != "CLOSE_BRACKETS" &&
			peek().type !="EOF")
		{
			body.push_back(parseStatement());
		}
		if (!match("CLOSE_BRACKETS")) {
			syntaxError(peek(), "ต้องมี } หลังคำสั่ง ที่ ถ้า");
		}
		vector<ASTNodePtr> elifs;
		while (peek().type == "ELIF") {
			elifs.push_back(parseElif());
		}
		ASTNodePtr elsE = nullptr;
		if (peek().type == "ELSE") {
			elsE = parseElse();
		}
		return make_shared<IfNode>(cond, body, t, elifs, elsE);
	}
	ASTNodePtr parseElse() {
		Token t = peek();
		if (!match("ELSE")) {
			syntaxError(peek(), "ไม่พบคำสั่ง มิฉะนั้น");
		}

		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(), "ต้องมี { ก่อนคำสั่ง ที่ มิฉะนั้น");
		}
		vector<ASTNodePtr> body;
		while (
			peek().type != "CLOSE_BRACKETS" &&
			peek().type !="EOF")
		{
			body.push_back(parseStatement());
		}
		if (!match("CLOSE_BRACKETS")) {
			syntaxError(peek(), "ต้องมี } หลังคำสั่ง ที่ มิฉะนั้น");
		}
		return make_shared<ElseNode>(body, t);
	}

	ASTNodePtr parseFunctionDef() {
		Token t = peek();
		if (!match("PROGRAM")) {
			syntaxError(peek(), "ไม่พบคำสั่ง โปรแกรม");
		}
		if (peek().type != "IDENTIFIER") {
			syntaxError(peek(),"ขาดชื่อ โปรแกรม ในการประกาศโปรแกรม");
		}
		string funcname = advance().value;
		if (!match("OPEN_PAREN")) {
			syntaxError(peek(), "ขาด ( ในการประกาศโปรแกรม");
		}
		vector<ASTNodePtr> pramas;
		while (peek().type != "CLOSE_PAREN" &&
			   peek().type != "EOF") {
			//pramas.push_back(parseVariableDecleartion());
			Token t = peek();
			if (!match("DECLARE")) {
				syntaxError(peek(),"ขาด ให้ ในการกำหนดพารามิเตอร์");
			}
			if (peek().type !=
				"IDENTIFIER") {
				syntaxError(peek(),"ไม่มีชื่อตัวแปร");
			}
			ASTNodePtr varname = parsevariable();
			ASTNodePtr initialValue = nullptr;
			string modifier = "";
			if (peek().type =="IDENTIFIER") {
				syntaxError(peek(),"ไม่มีคำสั่งนี่อยู่");
			}
			if (peek().type == "BE") {
				syntaxError(peek(), "ไม่สามารถกำหนด modifier ให้กับ พารามิเตอร์ได้");
			}
			if (peek().type =="EQUALSSIGN") {
				advance();
				initialValue = parseExpression();
			}

			pramas.push_back(make_shared<VariableDeclaretionNode>(varname, "have no", modifier,
														initialValue, t));
			if (!match("COMMA")) {
				break;
			}
		}
		if (!match("CLOSE_PAREN")) {
			syntaxError(peek(), "ขาด ( ในการประกาศโปรแกรม");
		}
		if (!match("OPEN_BRACKETS")) {
			syntaxError(peek(),
						"ขาด { ในการประกาศโปรแกรม ที่" + funcname);
		}
		vector<ASTNodePtr> body;
		while (peek().type != "CLOSE_BRACKETS" &&
			   peek().type !="EOF")
		{
			body.push_back(parseStatement());
		}
		if (!match("CLOSE_BRACKETS")) {
			syntaxError(peek(),
						"ขาด } ในการประกาศโปรแกรม ที่" + funcname);
		}
		return make_shared<FunctionDeclaretionNode>(funcname, pramas, body,
													"none", t);
	}
	ASTNodePtr parseInput() {
		Token t = peek();
		if (!match("INPUT")) {
			syntaxError(peek(), "ขาด คำสั่ง รับข้อมูล");
		}
		if (peek().type != "IDENTIFIER") {
			syntaxError(peek(), "ขาดชื่อตัวแปร");
		}
		ASTNodePtr varname = parsevariable();
		if (!match("SEMICOLON")) {
			syntaxError(peek(), "ต้องมี ; หลังจบคำสั่ง ที่ รับข้อมูล ");
		}
		return make_shared<InputNode>(varname, t);
	}
	ASTNodePtr parseReturn() {
		Token t = peek();
		if (!match("RETURN")) {
			syntaxError(peek(), "ขาด คำสั่ง คืนค่า");
		}
		ASTNodePtr value = nullptr;
		if (peek().type != "SEMICOLON") {
			value = parseExpression();
		}
		if (!match("SEMICOLON")) {
			syntaxError(peek(), "ต้องมี ; หลังจบคำสั่ง ที่คืนค่า ");
		}
		return make_shared<ReturnNode>(value, t);
	}
	ASTNodePtr parseImport() {
		Token t = peek();
		if (!match("IMPORT")) {
			syntaxError(peek(), "ขาด คำสั่ง นำเข้า");
		}
		if (peek().type != "STRING_VALUE") {
			syntaxError(peek(), "ขาด ชื่อไฟล์");
		}
		string filename = advance().value;
		if (!match("AS")) {
			syntaxError(peek(), "ขาด แทน");
		}
		string name = advance().value;
		if (!match("SEMICOLON")) {
			syntaxError(peek(), "ต้องมี ; หลังจบคำสั่ง ที่นำเข้า ");
		}
		return make_shared<ImportNode>(filename, name, t);
	}

	ASTNodePtr parseExport() {
		// ตรวจว่า token แรกต้องเป็น EXPORT
		if (!match("EXPORT")) {
			syntaxError(peek(), "ขาด คำสั่ง ส่งออก");
		}

		Token t = peek();
		vector<ASTNodePtr> funcs;

		do {
			funcs.push_back(parseFunctionDef());

			// ถ้ามี EXPORT อีก ต้อง match() ด้วยเพื่อขยับตำแหน่ง
		} while (peek().type == "EXPORT" &&
				 match("EXPORT"));

		return make_shared<ExportNode>(funcs, t);
	}



	ASTNodePtr parseStatement() {
	    if (peek().type == "PROGRAM") {
	        return parseFunctionDef();
	    } else if (peek().type == "COMMENT") {
	        Token t = peek();
	        advance();
	        string commentText = tokens[pos - 1].value;
	        return make_shared<CommentNode>(commentText, t);
	    } else if (peek().type == "EXITPROCESS") {
	        Token t = peek();
	        advance();
	        if (!match("SEMICOLON")) {
	            syntaxError(peek(), "ขาด ; ที่ จบการทำงาน");
	        }
	        return make_shared<ExitProcessNode>(t);
	    } else if (peek().type == "DECLARE") {
	        return parseVariableDecleartion();
	    } else if (peek().type == "INPUT") {
	        return parseInput();
	    } else if (peek().type == "PRINT") {
	        return parsePrint();
	    } else if (peek().type == "IF") {
	        return parseIf();
	    } else if (peek().type == "DO") {
	        return parseDoWhile();
	    } else if (peek().type == "WHILE") {
	        return parseWhileloop();
	    } else if (peek().type == "FOR") {
	        return parseForLoop();
	    } else if (match("BREAK")) {
	        Token t = tokens[pos - 1];
	        if (!match("SEMICOLON")) {
	            syntaxError(peek(), "ขาด ; ที่ ออกจากการทำซ้ำ");
	        }
	        return make_shared<BreakNode>(t);
	    } else if (match("CONTINUE")) {
	        Token t = tokens[pos - 1];
	        if (!match("SEMICOLON")) {
	            syntaxError(peek(), "ขาด ; ที่ ไปยังรอบถัดไป");
	        }
	        return make_shared<ContinueNode>(t);
	    } else if (peek().type == "ELIF") {
	        return parseElif();
	    } else if (peek().type == "ELSE") {
	        return parseElse();
	    } else if (peek().type == "RETURN") {
	        return parseReturn();
	    } else if (peek().type == "IMPORT") {
	        return parseImport();
	    } else if (peek().type == "EXPORT") {
	        return parseExport();
	    } else if (peek().type == "IDENTIFIER") {
	        // ใช้ parsepimary() ที่มีอยู่แล้วซึ่งจัดการ chain อยู่แล้ว
	        ASTNodePtr expr = parseExpression();

	        if (!match("SEMICOLON")) {
	            syntaxError(peek(), "ขาด ; ที่ จบคำสั่ง");
	        }
	        return expr;
	    }



	    stringstream ss;
	    ss << "คำสั่งไม่รู้จัก: '" << peek().value
	       << "' (ชนิด: " << peek().type << ")";
	    syntaxError(peek(), ss.str());
	    return nullptr;
	}
};
size_t count_utf8_chars(const string &str) {
	size_t count = 0;
	for (size_t i = 0; i < str.length();) {
		unsigned char c = static_cast<unsigned char>(str[i]);
		if (c <= 0x7F) {
			i += 1;
			count++;
		} else if (c >= 0xC0 && c <= 0xDF) {
			i += 2;
			count++;
		} else if (c >= 0xE0 && c <= 0xEF) {
			i += 3;
			count++;
		} else if (c >= 0xF0 && c <= 0xF7) {
			i += 4;
			count++;
		} else {
			// Invalid UTF-8, treat as single character
			i += 1;
			count++;
		}
	}
	return count;
}

void update_counters(const string &str, size_t &line, size_t &col) {
	// Simplified: each token counts as 1 column
	for (char c : str) {
		if (c == '\n') {
			line++;
			col = 1; // Reset column at new line
		}
	}
	col++; // Each token advances column by 1
}


// command base
vector<pair<string, string>> TokenPatterns = {
	{"COMMENT", R"(#[\s\S]*?#)"},
	{"PROGRAM", "โปรแกรม"},
	{"EXITPROCESS", "จบการทำงาน"},
	{"OPEN_BRACKETS", "\\{"},
	{"CLOSE_BRACKETS", "\\}"},
	{"DECLARE", "ให้"},
	{"INTEGER", "จำนวนเต็ม"},
	{"FLOAT", "ทศนิยม"},
	{"STRING", "ข้อความ"},
	{"ARRAY", "ชุดข้อมูล"},
	{"OBJECT", "อ็อบเจกต์"},
	{"CONST", "ค่าคงที่"},
	{"BOOLLEAN", "ค่าความจริง"},
	{"NULL", "ว่าง"},
	{"CONVERTDATATYPE", "เปลี่ยนชนิดข้อมูล"},
	{"LENGTH", "ขนาด"},
	{"POP", "ดึงออก"},
	{"PUSH", "เพิ่ม"},
	{"INSERT", "แทรก"},
	{"ERASE", "ลบ"},
	{"BE", "เป็น"},
	{"ARROW", "->"},
	{"EQUALSSIGN", "คือ"},
	{"AS", "แทน"},
	{"INCREMENT", "\\+\\+"},
	{"ADDITION", "\\+"},
	{"DECREMENT", "--"},
	{"SUBTRACTION", "-"},
	{"STRING_VALUE", R"("(?:[^"\\]|\\.)*")"},
	{"FLOAT_VALUE", "-?\\d+\\.\\d+"},
	{"INTEGER_VALUE", "-?\\d+"},
	{"ROOT", "ราก"},
	{"TRUE_VALUE", "จริง"},
	{"FALSE_VALUE", "เท็จ"},
	{"BRACKET_OPENING", "\\["},
	{"BRACKET_CLOSEING", "\\]"},
	{"EXPONENTIATION", "\\*\\*"},
	{"MULTIPLICATION", "\\*"},
	{"FLOORDIVISION", "//"},
	{"DIVISION", "/"},
	{"MODULAS", "%"},
	{"SHIFT_LEFT", "<<"},
	{"SHIFT_RIGHT", ">>"},
	{"EQUALTO", "="},
	{"NOTEQUAL", "!="},
	{"GREATEROREQUAL", ">="},
	{"LESSEROREQUAL", "<="},
	{"LN", "ln"},
	{"GREATER", ">"},
	{"LESSER", "<"},
	{"BITWISE_NOT", "!"},
	{"BITWISE_AND", "&"},
	{"BITWISE_OR", "\\|"},
	{"NOT", "ไม่"},
	{"OR", "หรือ"},
	{"AND", "และ"},
	{"XOR", "ซอร์"},
	{"DOT", "\\."},
	{"COMMA", ","},
	{"INPUT", "รับข้อมูล"},
	{"PRINT", "แสดงผล"},
	{"IF", "ถ้า"},
	{"OPEN_PAREN", "\\("},
	{"CLOSE_PAREN", "\\)"},
	{"STATEMENT1", "ตั้งแต่"},
	{"STATEMENT2", "จนถึง"},
	{"STATEMENT3", "โดยแต่ละรอบ"},
	{"WHILE", "ขณะ"},
	{"FOR", "ทำซ้ำ"},
	{"DO", "ทำ"},
	{"BREAK", "ออกจากการทำซ้ำ"},
	{"CONTINUE", "ไปยังรอบถัดไป"},
	{"ELIF", "มิฉะนั้นถ้า"},
	{"ELSE", "มิฉะนั้น"},
	{"VOID", "เปล่า"},
	{"RETURN", "คืนค่า"},
	{"IMPORT", "นำเข้า"},
	{"EXPORT", "ส่งออก"},
	{"COLON", ":"},
	{"SEMICOLON", ";"},
	{"IDENTIFIER", "[a-zA-Zก-๙_][a-zA-Zก-๙0-9_@$]*"}};

vector<Token> lexer(const string &code) {
	vector<Token> tokens;
	string remaining = code;
	int current_line = 1;
	int current_col = 1;

	// Precompile regex patterns
	vector<pair<string, regex>> compiledPatterns;
	regex whitespace_reg("^\\s+"); // Handles all whitespace

	for (const auto &[type, pattern] : TokenPatterns) {
		try {
			compiledPatterns.emplace_back(type, regex("^" + pattern));
		} catch (const regex_error &e) {
			stringstream ss;
			ss << "Regex error in token: " << type << "\nPattern: " << pattern
			   << "\nError: " << e.what();
			lexerError(current_line, current_col, ss.str(), "");
		}
	}

	while (!remaining.empty()) {
		// Skip whitespace - update line/col for newlines
		smatch whitespace_match;
		if (regex_search(remaining, whitespace_match, whitespace_reg)) {
			string whitespace = whitespace_match.str();
			for (char c : whitespace) {
				if (c == '\n') {
					current_line++;
					current_col = 1;
				}
			}
			remaining = whitespace_match.suffix().str();
			continue;
		}

		bool matched = false;
		for (const auto &[type, reg] : compiledPatterns) {
			smatch match;
			if (regex_search(remaining, match, reg)) {
				string token_value = match[0].str();
				int token_line = current_line;
				int token_col = current_col;

				// Create token with position info
				Token token{
					type, token_value, token_line,
					token_col // Token occupies one column
				};

				// Update position: each token counts as 1 column
				current_col++;

				// Update line counters for newlines in token
				for (char c : token_value) {
					if (c == '\n') {
						current_line++;
						current_col = 1;
					}
				}

				tokens.push_back(token);
				remaining = match.suffix().str();
				matched = true;
				break;
			}
		}

		if (!matched) {
			// Try to find the next whitespace or known delimiter
			size_t nextPos = remaining.find_first_of(" \t\n;.,(){}[]");
			string context = (nextPos == string::npos)
								 ? remaining
								 : remaining.substr(0, nextPos);

			lexerError(current_line, current_col, "ไม่พบคำสั่งนี้ในภาษา",
					   context);

			// Skip problematic character (UTF-8 aware)
			if (!remaining.empty()) {
				unsigned char c = static_cast<unsigned char>(remaining[0]);
				size_t skip_bytes = 1;
				if (c >= 0xC0 && c <= 0xDF)
					skip_bytes = 2;
				else if (c >= 0xE0 && c <= 0xEF)
					skip_bytes = 3;
				else if (c >= 0xF0 && c <= 0xF7)
					skip_bytes = 4;

				remaining = remaining.substr(skip_bytes);
				current_col++;
			}
		}
	}

	tokens.push_back({"EOF", "", current_line, current_col});
	return tokens;
}

Value evalFunctionFromParts(const vector<string> &params, const json &body,
                            const vector<Value> &args) {
    // Create new scope
    env.push_back(unordered_map<string, EnvStruct>());

    // Bind arguments to parameters
    for (size_t i = 0; i < params.size(); i++) {
        declare(params[i], args[i], false, 0, 0);
    }

    // Execute function body
    Value result = nullptr;
    try {
        for (const auto &stmt : body) {
            Value tmp = evalStatement(stmt);
            if (tmp) result = tmp;
        }
    } catch (const ReturnException &e) {
        result = e.returnValue;
    }

    // Clean up scope
    env.pop_back();
    return result;
}

void evalProgram(const json &programAST) {
	env.push_back({});
	if (programAST["type"] != "Program") {
		cerr << "AST ที่ส่งเข้า evalProgram ต้องเป็น Program node\n";
		exit(1);
	}

	  json exprArray = programAST["statements"];
	for (const auto &stmt : exprArray) {
		evalStatement(stmt);
	}

	env.pop_back();
}
Value evalFunctionFromNode(const json &funcNode, const vector<Value> &args) {
	// 1. ตรวจสอบว่าประเภทคือ functionDeclaretion
	if (funcNode["type"] != "functionDeclaretion") {
		cerr << "ไม่ใช่ฟังก์ชันที่สามารถเรียกได้" << "";
		std::exit(1);
	}

	// 2. สร้าง Environment ใหม่
	env.push_back({});

	// 3. ผูก arguments กับ parameter
	const auto &params = funcNode["parameter"];
	if (params.size() != args.size()) {
		cerr << "จำนวน arguments ไม่ตรงกับ parameter" << "";
		std::exit(1);
	}

	for (size_t i = 0; i < params.size(); i++) {
		string paramName = params[i]["variable"]["name"];
		string paramType = params[i]["datatype"];
		env.back()[paramName] = EnvStruct{args[i], false};
	}

	// 4. ประมวลผล statements
	const auto &statements = funcNode["body"]["statements"];
	for (const auto &stmt : statements) {
		Value ret = evalStatement(stmt);
		if (ret != nullptr) { // หาก return
			env.pop_back();
			return ret;
		}
	}

	env.pop_back();
	return make_shared<ValueHolder>(); // ถ้าไม่มี return ให้ส่ง null/monostate กลับ
}



string ast_json(string content){
			vector<Token> tokens = lexer(content);

			// Parse tokens
			Parser parser(tokens);
			auto ast = parser.parse();
			// Get AST JSON string
			string ast_json = ast->print();
			return ast_json;
}
std::string sanitize_for_json(const std::string& input) {
    std::string output;
    for (char c : input) {
        switch (c) {
            case '\r': output += "\\r"; break;
            case '\n': output += "\\n"; break;
            case '\t': output += "\\t"; break;
            default:   output += c;
        }
    }
    return output;
}

int main(int argc, char *argv[]) {
	
	ios::sync_with_stdio(false);
	cout.tie(nullptr);
    SetConsoleOutputCP(65001);

    if (argc < 2) {
        cerr << "Usage: mmt <filename>.thl [target.json]\nor mmt <filename>.thl" << "";
        exit(1);
    }

    string filename = argv[1];
    string fileTarget;  // กำหนดค่าว่างก่อน

    // เช็คกรณี version command
    if (filename == "-v" || filename == "-version") {
        cout << "mmt version 1.0 Runes of Thai" << "";
        return 0;
    }
    if (argc >= 3) {
        fileTarget = argv[2];
    }

    fs::path filepath = fs::current_path() / filename;

    // ตรวจสอบนามสกุลไฟล์ .thl
    if (filepath.extension() != ".thl") {
        cerr << "Invalid file extension. Must be .thl" << "";
        exit(1);
    }

    // ตรวจสอบไฟล์ .thl มีจริงไหม
    if (!fs::exists(filepath)) {
        cerr << "File not found: " << filename << "";
        exit(1);
    }


    // กรณีมี fileTarget ให้ตรวจสอบนามสกุลต้องเป็น .json เท่านั้น
    if (!fileTarget.empty()) {
        fs::path filepathTarget = fs::current_path() / fileTarget;
        if (filepathTarget.extension() != ".json") {
            cerr << "ไฟล์เป้าหมายต้องเป็น .json เท่านั้น" << "";
            exit(1);
        }
    }

    // อ่านไฟล์ .thl
    ifstream file(filename, ios::binary);
    if (!file.is_open()) {
        cerr << "Failed to open: " << filename << "";
        exit(1);
    }

    vector<string> lines;
    string line;
    while (getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    string content;
    for (const auto &l : lines) {
        content += l + "\n";
    }

    // Remove UTF-8 BOM if present
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
    }

    try {
        string astText = ast_json(content);
		//cout << astText<<"\n";
        if (fileTarget.empty()) {
			astText = sanitize_for_json(astText);
            json jsonWork = json::parse(astText);
            evalProgram(jsonWork);

        } else {
            // กรณี argc == 3 และไฟล์เป้าหมาย .json
            fs::path filepathTarget = fs::current_path() / fileTarget;
            fs::create_directories(filepathTarget.parent_path());

            ofstream ast_file(filepathTarget);
            if (ast_file.is_open()) {
                ast_file << astText;
                ast_file.close();
            } else {
                cerr << "Failed to write AST to: " << filepathTarget << "";
                exit(1);
            }
        }

    } catch (const exception &e) {
        cerr << "\nCompilation failed: " << e.what() << "";
        return 1;
    }

    return 0;
}

/*
this interpreter made by Mr.Phawat Matitham.
created at 2 Apr 2025.
*/
