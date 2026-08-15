// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuIntrospection.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace OscuTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/**
	 * A real game world, so the subsystems run through their actual lifecycle
	 * rather than a hand-poked approximation of one.
	 *
	 * OSC input is disabled by default. Otherwise every world a test creates would
	 * bind the configured listen port, which makes the suite non-hermetic and can
	 * collide with a real sender running on the same machine. Tests that need the
	 * socket ask for it explicitly.
	 */
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		explicit FScopedTestWorld(bool bEnableOSCInput = false)
		{
			Settings = GetMutableDefault<UOscuSettings>();
			bSavedEnableOSCIn = Settings->bEnableOSCIn;
			Settings->bEnableOSCIn = bEnableOSCInput;

			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);

			Settings->bEnableOSCIn = bSavedEnableOSCIn;
		}

		FScopedTestWorld(const FScopedTestWorld&) = delete;
		FScopedTestWorld& operator=(const FScopedTestWorld&) = delete;

		AActor* SpawnTagged(UClass* Class, std::initializer_list<const TCHAR*> Tags)
		{
			AActor* Actor = World->SpawnActor<AActor>(Class, FTransform::Identity);
			for (const TCHAR* Tag : Tags)
			{
				Actor->Tags.Add(FName(Tag));
			}
			return Actor;
		}

		template <typename T>
		T* SpawnTaggedAs(std::initializer_list<const TCHAR*> Tags)
		{
			return CastChecked<T>(SpawnTagged(T::StaticClass(), Tags));
		}

		/** Drives UWorld::BeginPlay, which is what calls OnWorldBeginPlay on every
		 *  world subsystem -- the registry's population trigger. */
		void BeginPlay() { World->BeginPlay(); }

		UOscuRouterSubsystem* Router() const { return UOscuRouterSubsystem::Get(World); }

	private:
		UOscuSettings* Settings = nullptr;
		bool bSavedEnableOSCIn = false;
	};

	inline const FOscuExposedFunctionInfo* FindFunction(const FOscuExposedTagInfo& TagInfo, const TCHAR* Name)
	{
		const FName Wanted(Name);
		return TagInfo.Functions.FindByPredicate(
			[Wanted](const FOscuExposedFunctionInfo& Info) { return Info.FunctionName == Wanted; });
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
