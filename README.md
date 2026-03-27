# Programowanie asynchroniczne w C++ - Szkolenie

## Linki

* https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer

## Ankieta

* https://forms.gle/KRBLb2kQm9vcCJyC9

## Dokumentacja

* https://infotraining.github.io/docs-cpp-async
* https://infotraining.github.io/docs-cpp-async/slides

## Konfiguracja środowiska

Proszę wybrać jedną z poniższych opcji:

### Lokalna

Przed szkoleniem należy zainstalować:

#### Kompilator C++:
  * GCC - Linux lub WSL
    * gcc (wersja >= 14)
    * [CMake > 3.25](https://cmake.org/)
      * proszę sprawdzić wersję w linii poleceń        
  
        ```
        cmake --version
        ```
    * vcpkg
      * instalacja - https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-bash
        * zklonować repozytorium vcpkg
          ```
          git clone https://github.com/microsoft/vcpkg.git
          ```
        * uruchomić skrypt bootstrap-vcpkg.sh
          ```
          cd vcpkg && ./bootstrap-vcpkg.sh
          ``` 
        * dodać zmienną środowiskową VCPKG_ROOT
          * w pliku `.bashrc` należy dodać wpis
          ```
          export VCPKG_ROOT=/path/to/vcpkg
          export PATH=$VCPKG_ROOT:$PATH
          ```        
        * odświeżyć zmienne środowiskowe
          ```
          source ~/.bashrc
          ```
        * zainstalować bibliotekę Catch2
          ```
          vcpkg install catch2
          ```
    * IDE: Visual Studio Code
      * [Visual Studio Code](https://code.visualstudio.com/)
      * zainstalować wtyczki
        * C/C++ Extension Pack
        * Live Share

### Docker + Visual Studio Code

Jeśli uczestnicy szkolenia korzystają w pracy z Docker'a, to należy zainstalować:

#### Docker Desktop (Windows)

* https://www.docker.com/products/docker-desktop/

#### Visual Studio Code

* [Visual Studio Code](https://code.visualstudio.com/)
* Zainstalować wtyczki
  * Live Share
  * Dev Containers ([wymagania](https://code.visualstudio.com/docs/devcontainers/containers#_system-requirements))
    * po instalacji wtyczki - należy otworzyć w VS Code folder zawierający sklonowane repozytorium i
      z palety poleceń (Ctrl+Shift+P) wybrać opcję **Dev Containers: Rebuild and Reopen in Container**


@startuml
skinparam state {
  BackgroundColor LightCyan
  BorderColor DarkSlateGray
}
hide empty description
title C++20 Coroutine & Awaiter Interface Lifecycle

[*] --> Invocation : Caller calls function

Invocation --> PromiseCreated : Frame allocated, args copied
PromiseCreated --> InitialSuspend : promise.get_return_object()

InitialSuspend --> Suspended : promise.initial_suspend()\nreturns suspend_always
InitialSuspend --> Running : returns suspend_never

Suspended --> Running : handle.resume()

state "Evaluating co_await (or co_yield)" as AwaiterFlow {
  state "awaiter.await_ready()" as AwaitReady
  state "State Saved" as StateSaved : Instruction pointer & registers saved
  state "awaiter.await_suspend(handle)" as AwaitSuspend
  state "awaiter.await_resume()" as AwaitResume
  
  [*] --> AwaitReady : Evaluate awaiter
  
  AwaitReady --> AwaitResume : returns true\n(value already available, skip suspend)
  AwaitReady --> StateSaved : returns false
  
  StateSaved --> AwaitSuspend 
  
  AwaitSuspend --> AwaitResume : returns false\n(resume immediately)
  AwaitSuspend --> SuspendedOuter : returns void or true\n(control returns to caller/resumer)
  AwaitSuspend --> SymmetricTransfer : returns coroutine_handle\n(resumes another coroutine)
  
  SuspendedOuter --> AwaitResume : externally resumed\nvia handle.resume()
  
  AwaitResume --> [*] : yields result to\nco_await expression
}

Running --> AwaiterFlow : hits co_await <expr>
AwaiterFlow --> Running : co_await finishes

Running --> FinalSuspend : co_return / unhandled exception

FinalSuspend --> SuspendedAtEnd : promise.final_suspend()\nreturns suspend_always
FinalSuspend --> [*] : returns suspend_never

SuspendedAtEnd --> [*] : handle.destroy()

' Invisible lines for layout formatting
SymmetricTransfer -[hidden]-> FinalSuspend
@enduml