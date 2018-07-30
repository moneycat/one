# -------------------------------------------------------------------------- #
# Copyright 2002-2018, OpenNebula Project, OpenNebula Systems                #
#                                                                            #
# Licensed under the Apache License, Version 2.0 (the "License"); you may    #
# not use this file except in compliance with the License. You may obtain    #
# a copy of the License at                                                   #
#                                                                            #
# http://www.apache.org/licenses/LICENSE-2.0                                 #
#                                                                            #
# Unless required by applicable law or agreed to in writing, software        #
# distributed under the License is distributed on an "AS IS" BASIS,          #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   #
# See the License for the specific language governing permissions and        #
# limitations under the License.                                             #
#--------------------------------------------------------------------------- #


require 'set'
require 'base64'
require 'zlib'
require 'pathname'
require 'yaml'
require 'opennebula'

$: << File.dirname(__FILE__)

include OpenNebula

module Migrator
    def db_version
        "5.6.0"
    end

    def one_version
        "OpenNebula 5.6.0"
    end

    def up
        feature_2228_user()

        return true
    end

    def feature_2228_user()
        @db.run "ALTER TABLE user_quotas RENAME TO old_user_quotas;"
        create_table(:user_quotas)

        @db.transaction do
            # oneadmin does not have quotas
            @db.fetch("SELECT * FROM old_user_quotas WHERE user_oid=0") do |row|
                @db[:user_quotas].insert(row)
            end

            @db.fetch("SELECT * FROM old_user_quotas WHERE user_oid>0") do |row|
                doc = Nokogiri::XML(row[:body],nil,NOKOGIRI_ENCODING){|c| c.default_xml.noblanks}

                calculate_quotas(doc, "uid=#{row[:user_oid]}", "User")

                @db[:user_quotas].insert(
                    :user_oid   => row[:user_oid],
                    :body       => doc.root.to_s)
            end
        end

        @db.run "DROP TABLE old_user_quotas;"
    end

    def calculate_quotas(doc, where_filter, resource)
        oid = doc.root.at_xpath("ID").text.to_i

        # VM quotas
        cpu_used = 0
        mem_used = 0
        vms_used = 0
        running_cpu_used = 0
        running_mem_used = 0
        running_vms_used = 0
        sys_used = 0

        # VNet quotas
        vnet_usage = {}

        # Image quotas
        img_usage = {}
        datastore_usage = {}

        @db.fetch("SELECT body FROM vm_pool WHERE #{where_filter} AND state<>6") do |vm_row|
            vmdoc = nokogiri_doc(vm_row[:body])

            # VM quotas
            mem_used, cpu_used, vms_used, running_mem_used, running_cpu_used, running_vms_used, sys_used = calculate_vm_quotas(vmdoc, mem_used, cpu_used, vms_used, running_mem_used, running_cpu_used, running_vms_used, sys_used)
            # VNet quotas
        end

        # VM quotas
        insert_vms_quotas(doc, resource, oid, running_mem_used, running_cpu_used, running_vms_used)
    end

    def calculate_vm_quotas(vmdoc, mem_used, cpu_used, vms_used, running_mem_used, running_cpu_used, running_vms_used, sys_used)

        vmdoc.root.xpath("TEMPLATE/CPU").each { |e|
            # truncate to 2 decimals
            cpu = (e.text.to_f * 100).to_i
            cpu_used += cpu
        }

        vmdoc.root.xpath("TEMPLATE/MEMORY").each { |e|
            mem_used += e.text.to_i
        }

        vms_used += 1

        vmdoc.root.xpath("STATE").each { |e|
            state = e.text.to_i
            if state == 1 || state == 2 || state == 3 # PENDING | HOLD | ACTIVE
                running_cpu_used = cpu_used
                running_mem_used = mem_used
                running_vms_used = vms_used
            end
        }

        return mem_used, cpu_used, vms_used, running_mem_used, running_cpu_used, running_vms_used, sys_used
    end

    def check_vms_quotas(doc, resource, oid, running_mem_used, running_cpu_used, running_vms_used)

        vm_elem = nil
        doc.root.xpath("VM_QUOTA/VM").each { |e| vm_elem = e }

        if !vm_elem.nil?

            running_cpu_used = (running_cpu_used / 100.0)
            running_cpu_used_str = sprintf('%.2f', running_cpu_used)

            vm_elem.add_child(doc.create_element("RUNNING_CPU")).content         = "-1"
            vm_elem.add_child(doc.create_element("RUNNING_CPU_USED")).content    = running_cpu_used_str

            vm_elem.add_child(doc.create_element("RUNNING_MEMORY")).content      = "-1"
            vm_elem.add_child(doc.create_element("RUNNING_MEMORY_USED")).content = running_mem_used.to_s

            vm_elem.add_child(doc.create_element("RUNNING_VMS")).content         = "-1"
            vm_elem.add_child(doc.create_element("RUNNING_VMS_USED")).content    = running_vms_used.to_s
        end
    end
end
