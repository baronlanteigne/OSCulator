// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuProjectCode.h"

#include "Interfaces/IProjectManager.h"
#include "Misc/PackageName.h"
#include "ProjectDescriptor.h"
#include "UObject/Class.h"
#include "UObject/Package.h"

namespace OscuProjectCode
{
	const TSet<FName>& GetProjectModuleNames()
	{
		// The project cannot change while the engine runs, so read the descriptor once.
		static const TSet<FName> Names = []()
		{
			TSet<FName> Out;
			if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
			{
				for (const FModuleDescriptor& Module : Project->Modules)
				{
					Out.Add(Module.Name);
				}
			}
			return Out;
		}();
		return Names;
	}

	bool IsDeclaredIn(const UFunction* Function, const TSet<FName>& Modules)
	{
		if (Function == nullptr || Modules.IsEmpty())
		{
			return false;
		}
		// The declaring class, not the class asked about: an inherited engine function's
		// outer is the engine class, so it never counts.
		const UClass* Declaring = Function->GetOuterUClass();
		if (Declaring == nullptr)
		{
			return false;
		}
		// Native classes live in /Script/<Module>.
		const FString PackageName = Declaring->GetOutermost()->GetName();
		if (!PackageName.StartsWith(TEXT("/Script/")))
		{
			return false;
		}
		return Modules.Contains(FName(*FPackageName::GetShortName(PackageName)));
	}
}
