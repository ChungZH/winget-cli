// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "winget/ThreadGlobals.h"
#include "ExecutionReporter.h"
#include "ExecutionArgs.h"
#include "ExecutionContextData.h"
#include "CompletionData.h"

#include <string_view>

#define WINGET_CATCH_RESULT_EXCEPTION_STORE(exceptionHR)   catch (const wil::ResultException& re) { exceptionHR = re.GetErrorCode(); }
#define WINGET_CATCH_HRESULT_EXCEPTION_STORE(exceptionHR)   catch (const winrt::hresult_error& hre) { exceptionHR = hre.code(); }
#define WINGET_CATCH_COMMAND_EXCEPTION_STORE(exceptionHR)   catch (const ::AppInstaller::CLI::CommandException&) { exceptionHR = APPINSTALLER_CLI_ERROR_INVALID_CL_ARGUMENTS; }
#define WINGET_CATCH_POLICY_EXCEPTION_STORE(exceptionHR)   catch (const ::AppInstaller::Settings::GroupPolicyException&) { exceptionHR = APPINSTALLER_CLI_ERROR_BLOCKED_BY_POLICY; }
#define WINGET_CATCH_STD_EXCEPTION_STORE(exceptionHR, genericHR)   catch (const std::exception&) { exceptionHR = genericHR; }
#define WINGET_CATCH_ALL_EXCEPTION_STORE(exceptionHR, genericHR)   catch (...) { exceptionHR = genericHR; }
#define WINGET_CATCH_STORE(exceptionHR, genericHR) \
        WINGET_CATCH_RESULT_EXCEPTION_STORE(exceptionHR) \
        WINGET_CATCH_HRESULT_EXCEPTION_STORE(exceptionHR) \
        WINGET_CATCH_COMMAND_EXCEPTION_STORE(exceptionHR) \
        WINGET_CATCH_POLICY_EXCEPTION_STORE(exceptionHR) \
        WINGET_CATCH_STD_EXCEPTION_STORE(exceptionHR, genericHR) \
        WINGET_CATCH_ALL_EXCEPTION_STORE(exceptionHR, genericHR)

// Terminates the Context with some logging to indicate the location.
// Also returns from the current function.
#define AICLI_TERMINATE_CONTEXT_ARGS(_context_,_hr_,_ret_) \
    do { \
        _context_.Terminate(_hr_, __FILE__, __LINE__); \
        return _ret_; \
    } while(0,0)

// Terminates the Context named 'context' with some logging to indicate the location.
// Also returns from the current function.
#define AICLI_TERMINATE_CONTEXT(_hr_)   AICLI_TERMINATE_CONTEXT_ARGS(context,_hr_,)

// Terminates the Context named 'context' with some logging to indicate the location.
// Also returns the specified value from the current function.
#define AICLI_TERMINATE_CONTEXT_RETURN(_hr_,_ret_) AICLI_TERMINATE_CONTEXT_ARGS(context,_hr_,_ret_)

namespace AppInstaller::CLI::Workflow
{
    struct WorkflowTask;
    enum class ExecutionStage : uint32_t;
}

namespace AppInstaller::CLI::Execution
{
    // bit masks used as Context flags
    enum class ContextFlag : int
    {
        None = 0x0,
        InstallerExecutionUseUpdate = 0x1,
        InstallerHashMatched = 0x2,
        InstallerTrusted = 0x4,
        AgreementsAcceptedByCaller = 0x8,
    };

    DEFINE_ENUM_FLAG_OPERATORS(ContextFlag);

    // The context within which all commands execute.
    // Contains input/output via Execution::Reporter and
    // arguments via Execution::Args.
    struct Context : EnumBasedVariantMap<Data, details::DataMapping>
    {
        Context(std::ostream& out, std::istream& in) : Reporter(out, in) {}

        // Clone the reporter for this constructor.
        Context(Execution::Reporter& reporter) : Reporter(reporter, Execution::Reporter::clone_t{}) {}

        virtual ~Context();

        // The path for console input/output for all functionality.
        Reporter Reporter;

        // The arguments given to execute with.
        Args Args;

        // Creates a copy of this context as it was at construction.
        virtual std::unique_ptr<Context> Clone();

        // Enables reception of CTRL signals.
        void EnableCtrlHandler(bool enabled = true);

        // Applies changes based on the parsed args.
        void UpdateForArgs();

        // Returns a value indicating whether the context is terminated.
        bool IsTerminated() const { return m_isTerminated; }

        // Gets the HRESULT reason for the termination.
        HRESULT GetTerminationHR() const { return m_terminationHR; }

        // Set the context to the terminated state.
        void Terminate(HRESULT hr, std::string_view file = {}, size_t line = {});

        // Set the termination hr of the context.
        void SetTerminationHR(HRESULT hr);

        // Cancel the context; this terminates it as well as informing any in progress task to stop cooperatively.
        // Multiple attempts with exitIfStuck == true may cause the process to simply exit.
        // The bypassUser indicates whether the user should be asked for cancellation (does not currently have any effect).
        void Cancel(bool exitIfStuck = false, bool bypassUser = false);

        // Gets context flags
        ContextFlag GetFlags() const
        {
            return m_flags;
        }

        // Set context flags
        void SetFlags(ContextFlag flags)
        {
            WI_SetAllFlags(m_flags, flags);
        }

        // Clear context flags
        void ClearFlags(ContextFlag flags)
        {
            WI_ClearAllFlags(m_flags, flags);
        }

        virtual void SetExecutionStage(Workflow::ExecutionStage stage, bool);

        // Get Globals for Current Thread
        AppInstaller::ThreadLocalStorage::ThreadGlobals& GetThreadGlobals();

#ifndef AICLI_DISABLE_TEST_HOOKS
        // Enable tests to override behavior
        virtual bool ShouldExecuteWorkflowTask(const Workflow::WorkflowTask&) { return true; }
#endif

    private:
        DestructionToken m_disableCtrlHandlerOnExit = false;
        bool m_isTerminated = false;
        HRESULT m_terminationHR = S_OK;
        size_t m_CtrlSignalCount = 0;
        ContextFlag m_flags = ContextFlag::None;
        Workflow::ExecutionStage m_executionStage = Workflow::ExecutionStage::Initial;
        AppInstaller::ThreadLocalStorage::ThreadGlobals m_threadGlobals;
    };
}
