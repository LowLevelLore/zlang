#include <all.hpp>

namespace zust {
    bool ScopeContext::defineVariable(const std::string &name,
                                      const VariableInfo &info) {
        if (!parent_ || (parent_->kind() == "Namespace" && kind() != "Function")) {
            if (lookupVariableInCurrentContext(name).has_value()) {
                return false;
            }
            TypeInfo ti = lookupType(info.type);
            vars_[name] = info;
            variable_name_mappings[name] = GLOBAL_NAME_MAPPER.mapVariable(name, name_);
            return true;
        } else {
            if (lookupVariableInCurrentContext(name).has_value()) {
                return false;
            }
            TypeInfo ti = lookupType(info.type);
            std::int64_t offset = allocateStack(name, ti);
            vars_[name] = info;
            offsetTable_[name] = offset;
            variable_name_mappings[name] = GLOBAL_NAME_MAPPER.mapVariable(name, name_);
            return true;
        }
    }
    void ScopeContext::defineFunction(const std::string &name, FunctionInfo info) {
        if (!info.isExtern) {
            info.label = GLOBAL_NAME_MAPPER.mapFunction(name, name_);
        }
        funcs_[name] = info;
    }
    void ScopeContext::defineType(const std::string &name, const TypeInfo &info) {
        types_[name] = info;
    }
    bool ScopeContext::defineParameterVariable(const std::string &name, const VariableInfo &info) {
        assert(kind() == "Function");
        std::shared_ptr<FunctionScope> parentFunction = findEnclosingFunctionScope();
        if (parentFunction->lookupVariableInCurrentContext(name).has_value()) {
            return false;
        }
        parentFunction->vars_[name] = info;
        parentFunction->variable_name_mappings[name] = GLOBAL_NAME_MAPPER.mapVariable(name, name_);
        return true;
    }
    void ScopeContext::defineParameter(const std::string &name, VariableInfo &info, CodegenOutputFormat format) {
        assert(kind() == "Function");
        std::shared_ptr<FunctionScope> parentFunction = findEnclosingFunctionScope();
        if (!parentFunction->lookupVariableInCurrentContext(name).has_value()) {
            throw std::runtime_error("Parameter " + name + ": Not defined, in scope: " + parentFunction->name_);
        }
        const std::vector<std::string> &XMM_POOL = (format == CodegenOutputFormat::X86_64_LINUX ? ARG_XMM_LINUX : ARG_XMM_MSVC);
        const std::vector<std::string> &GPR_POOL = (format == CodegenOutputFormat::X86_64_LINUX ? ARG_GPR_LINUX : ARG_GPR_MSVC);
        TypeInfo ti = lookupType(info.type);
        if (ti.isFloat) {
            // USE XMMs
            if (parentFunction->xmmCount < XMM_POOL.size()) {
                info.parameterInRegister = XMM_POOL[parentFunction->xmmCount];
                parentFunction->parameterRegisters.insert(info.parameterInRegister);
                info.parameterIsOnStack = false;
                parentFunction->xmmCount++;
            } else {
                std::int64_t offset = allocateStack(name, ti);
                info.parameterIsOnStack = true;
                offsetTable_[name] = offset;
            }
        } else {
            // USE GPRs
            if (parentFunction->gpCount < GPR_POOL.size()) {
                info.parameterInRegister = GPR_POOL[parentFunction->gpCount];
                parentFunction->parameterRegisters.insert(info.parameterInRegister);
                info.parameterIsOnStack = false;
                parentFunction->gpCount++;
            } else {
                std::int64_t offset = allocateStack(name, ti);
                info.parameterIsOnStack = true;
                offsetTable_[name] = offset;
            }
        }
        parentFunction->vars_[name] = info;
        parentFunction->paramCount++;
        logMessage("Defined Parameter");
    }
    VariableInfo ScopeContext::lookupVariable(const std::string &name) const {
        auto it = vars_.find(name);
        if (it != vars_.end()) {
            return it->second;
        }
        if (parent_) {
            // Skip outer function locals if crossing function boundary
            if (this->kind() == "Function" && parent_->kind() == "Function" &&
                parent_->parent_) {
                return parent_->parent_->lookupVariable(name);
            }
            return parent_->lookupVariable(name);
        }
        throw std::runtime_error("Undefined variable: " + name);
    }
    void ScopeContext::editVariable(const std::string &name, const VariableInfo &info) {
        auto it = vars_.find(name);
        if (it != vars_.end()) {
            vars_[name] = info;
            return;
        }
        if (parent_) {
            if (this->kind() == "Function" && parent_->kind() == "Function" &&
                parent_->parent_) {
                return parent_->parent_->editVariable(name, info);
            }
            return parent_->editVariable(name, info);
        }
        throw std::runtime_error("Undefined variable: " + name);
    }
    FunctionInfo ScopeContext::lookupFunction(const std::string &name) const {
        auto it = funcs_.find(name);
        if (it != funcs_.end()) {
            return it->second;
        }
        if (parent_) {
            return parent_->lookupFunction(name);
        }
        throw std::runtime_error("Undefined function: " + name);
    }
    TypeInfo ScopeContext::lookupType(const std::string &name) const {
        auto it = types_.find(name);
        if (it != types_.end()) {
            return it->second;
        }
        if (parent_) {
            return parent_->lookupType(name);
        }
        throw std::runtime_error("Undefined type: " + name);
    }
    std::optional<VariableInfo>
    ScopeContext::lookupVariableInCurrentContext(const std::string &name) const {
        auto it = vars_.find(name);
        if (it != vars_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    std::int64_t ScopeContext::allocateStack(const std::string & /*varName*/,
                                             const TypeInfo & /*type*/) {
        throw std::runtime_error("allocateStack not implemented for scope: " +
                                 kind());
    }
    std::int64_t ScopeContext::getVariableOffset(const std::string &name) const {
        auto it = offsetTable_.find(name);
        if (it != offsetTable_.end()) {
            return it->second;
        }
        if (parent_) {
            if (this->kind() == "Function" && parent_->kind() == "Function" &&
                parent_->parent_) {
                return parent_->parent_->getVariableOffset(name);
            }
            return parent_->getVariableOffset(name);
        }
        throw std::runtime_error("Unknown variable: " + name);
    }
    bool ScopeContext::isGlobalVariable(const std::string &name) const {
        const ScopeContext *ctx = this;
        while (ctx) {
            auto it = ctx->vars_.find(name);
            if (it != ctx->vars_.end()) {
                return ctx->isGlobalScope();
            }
            ctx = ctx->parent_.get();
        }
        throw std::runtime_error("Unknown variable: " + name);
    }
    bool ScopeContext::isGlobalScope() const { return parent_ == nullptr; }
    std::string ScopeContext::getMapping(std::string name) {
        auto it = variable_name_mappings.find(name);
        if (it != variable_name_mappings.end()) {
            return it->second;
        }
        if (parent_) {
            if (this->kind() == "Function" && parent_->kind() == "Function" &&
                parent_->parent_) {
                return parent_->parent_->getMapping(name);
            }
            return parent_->getMapping(name);
        }
        throw std::runtime_error("Mapping not found for variable: " + name);
    }
    void ScopeContext::setMapping(const std::string &name,
                                  const std::string &llvmName) {
        variable_name_mappings[name] = llvmName;
    }
    void ScopeContext::printScope(std::ostream &out, int indent) const {
        std::string pad(indent, ' ');
        out << pad << kind() << " Scope: " << name_ << "\n";

        if (!vars_.empty()) {
            out << pad << "  Variables:\n";
            for (const auto &kv : vars_) {
                out << pad << "    " << kv.first << ": " << kv.second.type << "\n";
            }
        }

        if (!funcs_.empty()) {
            out << pad << "  Functions:\n";
            for (const auto &kv : funcs_) {
                out << pad << "    " << kv.first << " -> " << kv.second.returnType
                    << "\n";
            }
        }

        if (!types_.empty()) {
            out << pad << "  Types:\n";
            for (const auto &kv : types_) {
                out << pad << "    " << kv.first << "\n";
            }
        }
    }
    std::shared_ptr<FunctionScope> ScopeContext::findEnclosingFunctionScope() {
        std::shared_ptr<ScopeContext> current = shared_from_this();
        while (current) {
            if (current->kind() == "Function") {
                return std::static_pointer_cast<FunctionScope>(current);
            }
            current = current->parent_;
        }
        return nullptr;
    }
    std::shared_ptr<ScopeContext> ScopeContext::getGlobal() {
        if (!parent_) {
            return shared_from_this();
        } else {
            return parent_->getGlobal();
        }
    }

    FunctionScope::FunctionScope(std::string name,
                                 std::shared_ptr<ScopeContext> parent)
        : ScopeContext(std::move(name), std::move(parent)), stackOffset_(-16) {}
    FunctionScope::~FunctionScope() = default;
    std::int64_t FunctionScope::allocateStack(const std::string &varName,
                                              const TypeInfo &type) {
        std::int64_t size = alignSize(type);
        stackOffset_ -= size;
        offsetTable_[varName] = stackOffset_;
        return stackOffset_;
    }
    void FunctionScope::printScope(std::ostream &out, int indent) const {
        // Use base implementation for printing variables, functions, and types
        ScopeContext::printScope(out, indent);
    }
    std::int64_t FunctionScope::getStackOffset() const {
        return stackOffset_;
    }
    std::string FunctionScope::allocateSpillSlot(std::int64_t size, CodegenOutputFormat format) {
        for (auto it = freeSpillSlots_.begin(); it != freeSpillSlots_.end(); ++it) {
            if (it->second == size) {
                std::int64_t offset = it->first;
                freeSpillSlots_.erase(it);
                switch (format) {
                case CodegenOutputFormat::X86_64_LINUX:
                    return "-" + std::to_string(offset) + "(%rbp)";
                case CodegenOutputFormat::X86_64_MSWIN:
                    return "[rbp - " + std::to_string(stackOffset_ + offset) + "]";
                default:
                    return "";
                }
            }
        }
        nextSpillOffset_ -= size;
        std::int64_t offset = nextSpillOffset_;
        switch (format) {
        case CodegenOutputFormat::X86_64_LINUX:
            return std::to_string(offset) + "(%rbp)";
        case CodegenOutputFormat::X86_64_MSWIN:
            return "[rbp - " + std::to_string(std::abs(stackOffset_ + offset)) + "]";
        default:
            return "";
        }
    }
    std::int64_t FunctionScope::getSpillSize() const {
        return nextSpillOffset_;
    }
    void FunctionScope::freeSpillSlot(const std::string &slot, std::int64_t size) {
        std::int64_t offset = 0;
        if (!slot.empty() && slot.front() == '[') {
            auto close = slot.find(']');
            if (close == std::string::npos)
                throw std::runtime_error("Invalid MASM spill slot format: " + slot);

            std::string inside = slot.substr(1, close - 1);
            auto opPos = inside.find_first_of("+-", 3);
            if (opPos == std::string::npos)
                throw std::runtime_error("No offset operator in MASM spill slot: " + slot);

            char op = inside[opPos];
            std::string numStr = inside.substr(opPos + 1);
            numStr.erase(0, numStr.find_first_not_of(" \t"));
            numStr.erase(numStr.find_last_not_of(" \t") + 1);

            offset = std::stoll(numStr);
            if (op == '-')
                offset = -offset;
        } else {
            // existing style: "(<offset>)", e.g. "-8(rbp)"
            auto paren = slot.find('(');
            if (paren == std::string::npos)
                throw std::runtime_error("Invalid spill slot format: " + slot);

            // e.g. slot = "-8(rbp)" → offsetStr = "-8"
            std::string offsetStr = slot.substr(0, paren);
            offset = std::stoll(offsetStr);
        }

        freeSpillSlots_.emplace_back(offset, size);
    }

    BlockScope::BlockScope(std::string name,
                           std::shared_ptr<FunctionScope> funcScope,
                           std::shared_ptr<ScopeContext> parent)
        : ScopeContext(std::move(name), std::move(parent)),
          funcScope_(std::move(funcScope)) {}
    BlockScope::~BlockScope() = default;
    std::int64_t BlockScope::allocateStack(const std::string &varName,
                                           const TypeInfo &type) {
        return funcScope_->allocateStack(varName, type);
    }
    void BlockScope::printScope(std::ostream &out, int indent) const {
        std::string pad(indent, ' ');
        out << pad << kind() << " Scope: " << name_ << "\n";
        ScopeContext::printScope(out, indent + 2);
    }
}  // namespace zust
