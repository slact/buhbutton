# cat hello_world.rb
require "cuba"
require 'json'
require 'rack/json'
require "pry"
require 'net/http'

Cuba.use Rack::JSON

PUSHMODULE_PUBLISH_URL = "http://localhost:9094/pub"
nginx_pid=spawn "nginx -c $(pwd)/nginx.conf"

def pushmodule_publish(data)
  uri = URI(PUSHMODULE_PUBLISH_URL)
  req = Net::HTTP::Post.new(uri, initheader = {'Content-Type' =>'application/json'})
  req.body = data.to_json
  response = Net::HTTP.new(uri.hostname, uri.port).start {|http| http.request(req) }
end

Cuba.define do
  on get do
    on "hello" do
      #binding.pry
      res.headers["Content-Type"]='application/json; charset=utf-8'
      res.write({id:'foobar', subscribe_url:'http://localhost:9094/sub'}.to_json)
    end
    on "alert" do
      res.write <<-EOS
        <html>
          <body>
            <h2>Submit alert url</h2>
            <form method="post">
              <input type="text" name="url" value="https://duckduckgo.com"></input>
              <input type="submit"></input>
            </form>
          </body>
        </html>
      EOS
    end
  end
  on post do
    on "alert" do
      on param("url") do |url|
        pushmodule_publish action: "alert", url: url
        res.headers["Content-Type"]='text/plain; charset=utf-8'
        res.write "very nice."
      end
      on true do
        pushmodule_publish action: "alert", url: "http://example.com"
        res.headers["Content-Type"]='text/plain; charset=utf-8'
        res.write "rather plain."
      end
    end
  end
  on default { res.status = 404; res.write("404 not found") }
end
