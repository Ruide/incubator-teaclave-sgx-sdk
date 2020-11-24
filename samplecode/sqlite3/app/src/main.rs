// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License..

extern crate sgx_types;
extern crate sgx_urts;
use sgx_types::*;
use sgx_urts::SgxEnclave;
use std::io::{self, Write};

static ENCLAVE_FILE: &'static str = "enclave.signed.so";

extern {
    fn ecall_opendb(eid: sgx_enclave_id_t, retval: *mut sgx_status_t, dbname: *const i8) -> sgx_status_t;
    fn ecall_execute_sql(eid: sgx_enclave_id_t, retval: *mut sgx_status_t, sql: *const i8) -> sgx_status_t;
    fn ecall_closedb(eid: sgx_enclave_id_t, retval: *mut sgx_status_t) -> sgx_status_t;
}

#[no_mangle]
pub unsafe extern "C" fn ocall_print_error(content: *const i8){
    let slice = std::ffi::CStr::from_ptr(content);
    eprintln!("{}", slice.to_str().unwrap());
}

#[no_mangle]
pub unsafe extern "C" fn ocall_print_string(content: *const i8){
    let slice = std::ffi::CStr::from_ptr(content);
    print!("{}", slice.to_str().unwrap());
}

#[no_mangle]
pub unsafe extern "C" fn ocall_println_string(content: *const i8){
    let slice = std::ffi::CStr::from_ptr(content);
    println!("{}", slice.to_str().unwrap());
}

fn init_enclave() -> SgxResult<SgxEnclave> {
    let mut launch_token: sgx_launch_token_t = [0; 1024];
    let mut launch_token_updated: i32 = 0;
    // call sgx_create_enclave to initialize an enclave instance
    // Debug Support: set 2nd parameter to 1
    let debug = 1;
    let mut misc_attr = sgx_misc_attribute_t {secs_attr: sgx_attributes_t { flags:0, xfrm:0}, misc_select:0};
    SgxEnclave::create(ENCLAVE_FILE,
                       debug,
                       &mut launch_token,
                       &mut launch_token_updated,
                       &mut misc_attr)
}

fn main() {
    if std::env::args().count() != 2 {
        print!("Usage: ");
        println!("{} [database file]", std::env::args().nth(0).unwrap());
        println!("Note: put :memory: in [database file] for in-memory database");
        return;
    }

    let enclave = match init_enclave() {
        Ok(r) => {
            println!("[+] Init Enclave Successful {}!", r.geteid());
            r
        },
        Err(x) => {
            println!("[-] Init Enclave Failed {}!", x.as_str());
            return;
        },
    };

    println!("[+] Info: SQLite SGX enclave successfully created.");

    let database_name = std::env::args().nth(1).unwrap();
    let mut retval = sgx_status_t::SGX_SUCCESS;

    let result = unsafe {
        // Open SQLite database
        ecall_opendb(enclave.geteid(), &mut retval, database_name.as_ptr() as * const i8)
    };
    match result {
        sgx_status_t::SGX_SUCCESS => {
            println!("[+] Info: SQLite SGX DB successfully opened.");
            println!("[+] Info: Enter SQL statement to execute or 'quit' or Ctrl + d to exit:");
        },
        _ => {
            println!("[-] ECALL Open DB Failed {}!", result.as_str());
            return;
        }
    }

    loop {
        print!("> ");
        let _ = io::stdout().flush();
    
        let mut input = String::new();
        match io::stdin().read_line(&mut input) {
            Ok(n) => {
                // for Ctrl-D EOF case
                if n == 0 {
                    break
                }

                if input == "\n" {
                    continue
                }

                if input == "quit\n" {
                    break
                }

                let result = unsafe {
                    ecall_execute_sql(enclave.geteid(), &mut retval, input.as_ptr() as * const i8)
                };

                match result {
                    sgx_status_t::SGX_SUCCESS => {},
                    _ => {
                        println!("[-] ECALL Execute SQL Failed {}!", result.as_str());
                        return;
                    }
                }
            }
            Err(error) => {
                println!("error: {}", error);
                break;
            }
        }    
    }

    // Closing SQLite database inside enclave
    let result = unsafe {
        ecall_closedb(enclave.geteid(), &mut retval)
    };
    match result {
        sgx_status_t::SGX_SUCCESS => {},
        _ => {
            println!("[-] ECALL Enclave Failed {}!", result.as_str());
            return;
        }
    }

    /* Destroy the enclave */
    enclave.destroy();
    println!("[+] Info: SQLite SGX DB successfully closed.");
}
