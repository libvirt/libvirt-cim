<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output method="xml" encoding="ISO-8859-1" indent="yes"
      doctype-public="-//W3C//DTD XHTML 1.0 Transitional//EN"
      doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"/>

  <xsl:variable name="href_base" select="''"/>
  <xsl:variable name="menu_name">Main Menu</xsl:variable>
<!--
 - returns the filename associated to an ID in the original file
 -->
  <xsl:template name="filename">
    <xsl:param name="name" select="string(@href)"/>
    <xsl:choose>
      <xsl:when test="$name = '#Introducti'">
        <xsl:text>intro.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Documentat'">
        <xsl:text>docs.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Reporting'">
        <xsl:text>development.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#help'">
        <xsl:text>help.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Help'">
        <xsl:text>help.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Errors'">
        <xsl:text>errors.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Downloads'">
        <xsl:text>downloads.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#News'">
        <xsl:text>news.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Contributi'">
        <xsl:text>development.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#architecture'">
        <xsl:text>architecture.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#FAQ'">
        <xsl:text>FAQ.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Platforms'">
        <xsl:text>platforms.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Schema'">
	<xsl:text>schema.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = '#Patches'">
        <xsl:text>patches.html</xsl:text>
      </xsl:when>
      <xsl:when test="$name = ''">
        <xsl:text>unknown.html</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="$name"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>
<!--
 - The global title
 -->
  <xsl:variable name="globaltitle" select="string(/html/body/h1[1])"/>

<!--
  the main menu box
 -->
  <xsl:template name="linkList">
  <div class="linkList2">
    <div class="llinks2">
      <h3 class="links2"><span>main menu</span></h3>
      <ul>
        <li><a href="{$href_base}index.html">Home</a></li>
    <xsl:for-each select="/html/body/h2">
    <xsl:variable name="filename">
      <xsl:call-template name="filename">
	<xsl:with-param name="name" select="concat('#', string(a[1]/@name))"/>
      </xsl:call-template>
    </xsl:variable>
    <xsl:if test="$filename != ''">
      <li>
	<xsl:element name="a">
	  <xsl:attribute name="href">
	    <xsl:value-of select="$filename"/>
	  </xsl:attribute>
	  <xsl:if test="$filename = 'docs.html'">
	      <xsl:attribute name="style">font-weight:bold</xsl:attribute>
	  </xsl:if>
	  <xsl:value-of select="."/>
	</xsl:element>
      </li>
    </xsl:if>
    </xsl:for-each>
      </ul>
    </div>
    <div class="llinks2">
      <h3 class="links2"><span>related links</span></h3>
      <ul>
        <li> <a href="https://www.redhat.com/mailman/listinfo/libvirt-cim/">Mailing list</a></li>
	<li> <a href="http://libvirt.org/">libvirt</a></li>
        <li><a href="http://xmlsoft.org/"> <img src="http://libvirt.org/Libxml2-Logo-90x34.gif" alt="Made with Libxml2 Logo" /></a></li>
      </ul>
      <p class='credits'>Graphics and design by <a href="mail:dfong@redhat.com">Diana Fong</a></p>
    </div>
  </div>
  </xsl:template>

<!--
  the page title
 -->

  <xsl:template name="titlebox">
    <xsl:param name="title"/>
    <h1 class="style1"><xsl:value-of select="$title"/></h1>
  </xsl:template>

<!--
 - Write the styles in the head
 -->
  <xsl:template name="style">
    <link rel="stylesheet" type="text/css" href="http://libvirt.org/libvirt.css" />
    <link rel="SHORTCUT ICON" href="/32favicon.png" />
  </xsl:template>

<!--
 - The top section 
 -->
  <xsl:template name="top">
    <div id="top">
      <img src="{$href_base}libvirtHeader.png" alt="Libvirt the virtualization API" />
    </div>
  </xsl:template>

<!--
 - The top section for the main page
 -->
  <xsl:template name="topmain">
    <div id="topmain">
      <img src="{$href_base}libvirtLogo.png" alt="Libvirt the virtualization API" />
    </div>
  </xsl:template>

<!--
 - The bottom section
 -->
  <xsl:template name="bottom">
    <div id="bottom">
      <p class="p1"></p>
    </div> 
  </xsl:template>

<!--
 - Handling of nodes in the body after an H2
 - Open a new file and dump all the siblings up to the next H2
 -->
  <xsl:template name="subfile">
    <xsl:param name="header" select="following-sibling::h2[1]"/>
    <xsl:variable name="filename">
      <xsl:call-template name="filename">
        <xsl:with-param name="name" select="concat('#', string($header/a[1]/@name))"/>
      </xsl:call-template>
    </xsl:variable>
    <xsl:variable name="title">
      <xsl:value-of select="$header"/>
    </xsl:variable>
    <xsl:document href="{$filename}" method="xml" encoding="ISO-8859-1"
      doctype-public="-//W3C//DTD XHTML 1.0 Transitional//EN"
      doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
      <html>
        <head>
          <xsl:call-template name="style"/>
          <xsl:element name="title">
            <xsl:value-of select="$title"/>
          </xsl:element>
        </head>
	<body>
	<div id="container">
	  <div id="intro">
	    <div id="adjustments"/>
	    <div id="pageHeader"/>
	    <div id="content2">
	      <xsl:call-template name="titlebox">
		<xsl:with-param name="title" select="$title"/>
	      </xsl:call-template>
	      <xsl:apply-templates mode="subfile" select="$header/following-sibling::*[preceding-sibling::h2[1] = $header and name() != 'h2' ]"/>
	    </div>
	  </div>
	  <xsl:call-template name="linkList"/>
	  <xsl:call-template name="bottom"/>
	</div>
	</body>
      </html>
    </xsl:document>
  </xsl:template>

  <xsl:template mode="subcontent" match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates mode="subcontent" select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template mode="content" match="@*|node()">
    <xsl:if test="name() != 'h1' and name() != 'h2'">
      <xsl:copy>
        <xsl:apply-templates mode="subcontent" select="@*|node()"/>
      </xsl:copy>
    </xsl:if>
  </xsl:template>

  <xsl:template mode="subfile" match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates mode="content" select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

<!--
 - Handling of the initial body and head HTML document
 -->
  <xsl:template match="body">
    <xsl:variable name="firsth2" select="./h2[1]"/>
    <body>
    <div id="container">
      <div id="intro">
	<div id="adjustments">
	  <p class="p1"></p>
	</div>
	<div id="content">
          <xsl:apply-templates mode="content" select="($firsth2/preceding-sibling::*)"/>
          <xsl:for-each select="./h2">
            <xsl:call-template name="subfile">
	      <xsl:with-param name="header" select="."/>
            </xsl:call-template>
          </xsl:for-each>
	</div>
      </div>
      <xsl:call-template name="linkList"/>
      <xsl:call-template name="bottom"/>
    </div>
    </body>
  </xsl:template>
  <xsl:template match="head">
  </xsl:template>
  <xsl:template match="html">
    <xsl:message>Generating the Web pages</xsl:message>
    <html>
      <head>
        <xsl:call-template name="style"/>
        <title>the virtualization API</title>
      </head>
      <xsl:apply-templates/>
    </html>
  </xsl:template>
</xsl:stylesheet>
