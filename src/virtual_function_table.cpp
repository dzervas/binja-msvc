#include <binaryninjaapi.h>

#include "virtual_function_table.h"
#include "object_locator.h"
#include "utils.h"

using namespace BinaryNinja;

VirtualFunctionTable::VirtualFunctionTable(BinaryView* view, uint64_t vftAddr)
{
	m_view = view;
	m_address = vftAddr;
}

std::vector<VirtualFunction> VirtualFunctionTable::GetVirtualFunctions()
{
	size_t addrSize = m_view->GetAddressSize();
	std::vector<VirtualFunction> vFuncs = {};
	BinaryReader reader = BinaryReader(m_view);
	reader.Seek(m_address);

	while (true)
	{
		uint64_t vFuncAddr = ReadIntWithSize(&reader, addrSize);
		auto funcs = m_view->GetAnalysisFunctionsForAddress(vFuncAddr);
		if (funcs.empty())
		{
			Ref<Segment> segment = m_view->GetSegmentAt(vFuncAddr);
			if (segment == nullptr)
			{
				// Last CompleteObjectLocator?
				break;
			}

			if (segment->GetFlags() & (SegmentExecutable | SegmentDenyWrite))
			{
				LogInfo("Discovered function from vtable reference -> %x", vFuncAddr);
				m_view->CreateUserFunction(m_view->GetDefaultPlatform(), vFuncAddr);
				funcs.emplace_back(m_view->GetAnalysisFunctionsForAddress(vFuncAddr).front());
			}
			else
			{
				// Hit the next CompleteObjectLocator
				break;
			}
		}

		for (auto func : funcs)
		{
			vFuncs.emplace_back(VirtualFunction(m_view, m_address, func));
		}
	}

	return vFuncs;
}

CompleteObjectLocator VirtualFunctionTable::GetCOLocator()
{
	std::vector<uint64_t> dataRefs = m_view->GetDataReferencesFrom(m_address - m_view->GetAddressSize());
	return CompleteObjectLocator(m_view, dataRefs.front());
}

Ref<Type> VirtualFunctionTable::GetType()
{
	QualifiedName typeName = QualifiedName(GetTypeName());
	Ref<Type> typeCache = Type::NamedType(m_view, typeName);

	if (m_view->GetTypeByName(typeName) == nullptr)
	{
		size_t addrSize = m_view->GetAddressSize();
		StructureBuilder vftBuilder = {};
		vftBuilder.SetPropagateDataVariableReferences(true);
		size_t vFuncIdx = 0;
		for (auto&& vFunc : GetVirtualFunctions())
		{
			// TODO: This needs to be fixed, must update vfunc type to this ptr to our structure.
			vftBuilder.AddMember(
				Type::PointerType(addrSize, vFunc.m_func->GetType(), true), "vFunc_" + std::to_string(vFuncIdx));
			vFuncIdx++;
		}

		m_view->DefineUserType(typeName, TypeBuilder::StructureType(&vftBuilder).Finalize());

		typeCache = Type::NamedType(m_view, typeName);
	}

	return typeCache;
}

Ref<Symbol> VirtualFunctionTable::CreateSymbol()
{
	Ref<Symbol> newFuncSym = new Symbol {DataSymbol, GetSymbolName(), m_address};
	m_view->DefineUserSymbol(newFuncSym);
	m_view->DefineUserDataVariable(m_address, GetType());
	return newFuncSym;
}

// Example: Animal::`vftable'
// If subobject this will return: Bird::`vftable'{for `Flying'}
std::string VirtualFunctionTable::GetSymbolName()
{
	auto coLocator = GetCOLocator();
	std::string className = coLocator.GetClassName();
	if (coLocator.IsSubObject())
		return className + "::`vftable'" + "{for `" + coLocator.GetAssociatedClassName() + "'}";
	return className + "::`vftable'";
}

// Example: Animal::VTable
// If subobject this will return the type name of the subobject type.
std::string VirtualFunctionTable::GetTypeName()
{
	return GetCOLocator().GetAssociatedClassName() + "::VTable";
}